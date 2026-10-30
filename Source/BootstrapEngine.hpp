#pragma once

#include "Algorithm.hpp"
#include "BOSS.hpp"
#include "Engine.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include "ThreadSafety.hpp"
#include "Utilities.hpp"

#include <filesystem>

#ifndef _WIN32
#include <dlfcn.h>
#else
#define NOMINMAX // max macro in minwindef.h interfering with std::max...
#include <windows.h>
constexpr static int RTLD_NOW = 0;
constexpr static int RTLD_NODELETE = 0;
static void* dlopen(LPCSTR lpLibFileName, int /*flags*/) {
  void* libraryPtr = LoadLibrary(lpLibFileName);
  if(libraryPtr != nullptr) {
    return libraryPtr;
  }
  // if it failed to load the standard way (searching dependent dlls in the exe path)
  // try one more time, with loading the dependent dlls from the dll path
  auto filepath = ::std::filesystem::path(lpLibFileName);
  if(filepath.is_absolute()) {
    return LoadLibraryEx(lpLibFileName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  } else {
    auto absFilepath = ::std::filesystem::absolute(filepath).string();
    LPCSTR lpAbsFileName = absFilepath.c_str();
    return LoadLibraryEx(lpAbsFileName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  }
}
static auto dlclose(void* hModule) { return FreeLibrary((HMODULE)hModule); }
static auto dlerror() {
  auto errorCode = GetLastError();
  LPSTR pBuffer = NULL;
  auto msg = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
                               FORMAT_MESSAGE_ALLOCATE_BUFFER,
                           NULL, errorCode, 0, (LPSTR)&pBuffer, 0, NULL);
  if(msg > 0) {
    // Assign buffer to smart pointer with custom deleter so that memory gets released
    // in case String's constructor throws an exception.
    auto deleter = [](void* p) { ::LocalFree(p); };
    ::std::unique_ptr<TCHAR, decltype(deleter)> ptrBuffer(pBuffer, deleter);
    return "(" + ::std::to_string(errorCode) + ") " + ::std::string(ptrBuffer.get(), msg);
  }
  return ::std::to_string(errorCode);
}
static void* dlsym(void* hModule, LPCSTR lpProcName) {
  return GetProcAddress((HMODULE)hModule, lpProcName);
}
#endif // _WIN32

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace boss {
namespace engines {
struct LibraryAndFunctions {
  using EntryPoint = BOSSExpression* (*)(BOSSExpression*);
  void* library;
  void (*resetFunction)(void);
  EntryPoint evaluateFunction;
};

// dlopen gives every BootstrapEngine in the process the same engine image, so all of them
// have to see each other's dispatches and reconfigurations. BOSS.cpp defines this state,
// because a static member of BootstrapEngine, which is declared in the anonymous namespace
// below, would exist once per translation unit.
struct EngineDispatchCoordination {
  boss::concurrency::SharedMutex engineStateMutex;
  std::atomic<std::size_t> evaluationsInFlight {0};
  std::atomic<bool> reconfigurationInProgress {false};
  // Libraries of destroyed BootstrapEngine instances, waiting to be reset and unloaded until no
  // dispatch is in flight. finishReconfiguration tears them down before it clears
  // reconfigurationInProgress.
  std::vector<LibraryAndFunctions> librariesAwaitingTeardown BOSS_GUARDED_BY(engineStateMutex);
};
EngineDispatchCoordination& engineDispatchCoordination();

namespace {
using boss::utilities::operator""_;

using std::for_each;
using std::make_move_iterator;
using std::prev;
using std::runtime_error;
using std::string;
using std::unordered_map;
using std::vector;
using std::filesystem::path;

using boss::ComplexExpression;
using boss::Engine;
using boss::Expression;
using boss::Symbol;
using boss::expressions::generic::visit;
using boss::utilities::overload;

/// Must run without engineStateMutex, because reset() is an engine call. Rethrows the first
/// exception from reset() after unloading every library.
void unloadLibraries(vector<LibraryAndFunctions> const& libs) {
  std::exception_ptr firstResetFailure;
  for(auto const& lib : libs) {
    if(lib.resetFunction != nullptr) {
      try {
        lib.resetFunction();
      } catch(...) {
        if(!firstResetFailure) {
          firstResetFailure = std::current_exception();
        }
      }
    }
    dlclose(lib.library);
  }
  if(firstResetFailure) {
    std::rethrow_exception(firstResetFailure);
  }
}

/// Threading contract of BootstrapEngine.
///
/// @par Callers
/// A chibi context is not thread-safe, so no two threads may evaluate in the same context at the
/// same time. Threads with separate contexts can call BOSSEvaluate concurrently, and
/// BootstrapEngine runs their evaluations concurrently.
///
/// @par Engines
/// An engine's evaluate and reset functions must be reentrant. BootstrapEngine can call one
/// engine from several threads at once and never makes one engine call wait for another.
/// BootstrapEngine loads an engine on the first dispatch to it, so the engine's static
/// initializers run on the thread of that dispatch. On Linux and macOS, dlopen uses
/// RTLD_NODELETE, so dlclose leaves the library mapped and a later load does not run the static
/// initializers again: reset is the only call in which an engine can clear its state.
///
/// @par Reconfiguration
/// SetDefaultEnginePipeline and ResetEngines replace the engine pipeline and reset and unload
/// every loaded engine. Both throw while an engine dispatch is in flight; bootstrap commands and
/// evaluations that reach no engine do not count as dispatches. No lock can make a
/// reconfiguration during a dispatch safe, because reset changes the state of an engine that
/// the dispatch is still using. While a reconfiguration is resetting and unloading engines,
/// BootstrapEngine refuses every new dispatch.
///
/// @par Destruction
/// Destroying a BootstrapEngine resets and unloads its engines. While a dispatch is in flight,
/// the destructor queues the libraries instead, the last in-flight dispatch resets and unloads
/// them, and BootstrapEngine refuses dispatches that start in between.
///
/// @par Locking
/// BootstrapEngine holds engineStateMutex only to copy the pipeline and to look up engine
/// functions, and never during an engine call. Holding the mutex during an engine call would
/// make engine calls wait for each other, and could deadlock when an engine calls BOSSEvaluate
/// during its evaluation.
class BootstrapEngine : public Engine {

  boss::concurrency::SharedMutex& engineStateMutex = engineDispatchCoordination().engineStateMutex;
  std::atomic<std::size_t>& evaluationsInFlight = engineDispatchCoordination().evaluationsInFlight;
  std::atomic<bool>& reconfigurationInProgress =
      engineDispatchCoordination().reconfigurationInProgress;

  /// Increments evaluationsInFlight for the lifetime of the object.
  ///
  /// The count changes under the shared lock, so that a reconfiguration that reads 0 under the
  /// exclusive lock can rely on no dispatch starting until the reconfiguration releases the lock.
  /// The destructor that brings the count to 0 resets and unloads the libraries that a destroyed
  /// instance queued.
  class InFlightGuard {
  public:
    /// Selects the constructor for a caller that already holds the shared lock.
    struct SharedLockHeld {};

    InFlightGuard(std::atomic<std::size_t>& counter,
                  std::atomic<bool> const& reconfigurationInProgress,
                  boss::concurrency::SharedMutex& mutex)
        : counter(counter), reconfigurationInProgress(reconfigurationInProgress), mutex(mutex) {
      boss::concurrency::SharedLock const lock(mutex);
      enter();
    }
    InFlightGuard(std::atomic<std::size_t>& counter,
                  std::atomic<bool> const& reconfigurationInProgress,
                  boss::concurrency::SharedMutex& mutex, SharedLockHeld /*unused*/)
        : counter(counter), reconfigurationInProgress(reconfigurationInProgress), mutex(mutex) {
      enter();
    }
    ~InFlightGuard() {
      bool lastDispatchBeforeQueuedTeardown = false;
      {
        boss::concurrency::SharedLock const lock(mutex);
        lastDispatchBeforeQueuedTeardown = --counter == 0 && reconfigurationInProgress.load();
      }
      if(lastDispatchBeforeQueuedTeardown) {
        finishReconfiguration(engineDispatchCoordination());
      }
    }
    InFlightGuard(InFlightGuard const&) = delete;
    InFlightGuard(InFlightGuard&&) = delete;
    InFlightGuard& operator=(InFlightGuard const&) = delete;
    InFlightGuard& operator=(InFlightGuard&&) = delete;

  private:
    std::atomic<std::size_t>& counter;
    std::atomic<bool> const& reconfigurationInProgress;
    boss::concurrency::SharedMutex& mutex;

    void enter() {
      if(reconfigurationInProgress.load()) {
        throw runtime_error(
            "Evaluation cannot start while SetDefaultEnginePipeline, ResetEngines or a destroyed "
            "BootstrapEngine is still resetting and unloading engines. Retry once the "
            "reconfiguration has finished.");
      }
      ++counter;
    }
  };

  /// Throws while a dispatch is in flight or another reconfiguration is running, and
  /// otherwise sets reconfigurationInProgress until the ReconfigurationEnd object of this
  /// reconfiguration clears it. The caller holds the exclusive lock, so no dispatch can start
  /// between the checks and the setting of the flag.
  void beginReconfiguration(char const* operatorName) {
    if(evaluationsInFlight.load() > 0) {
      throw runtime_error(
          string(operatorName) +
          " is only valid while no engine dispatch is in flight. Engine calls run without the "
          "engine-state lock, so a reconfiguration now would reset or unload an engine during a "
          "concurrent call. Wait until no engine dispatch is running.");
    }
    if(reconfigurationInProgress.load()) {
      throw runtime_error(
          string(operatorName) +
          " cannot run while another reconfiguration is still resetting and unloading engines.");
    }
    reconfigurationInProgress = true;
  }

  /// Does not throw, so that a destructor can call it.
  static void finishReconfiguration(EngineDispatchCoordination& coordination)
      BOSS_EXCLUDES(coordination.engineStateMutex) {
    while(true) {
      vector<LibraryAndFunctions> librariesToUnload;
      {
        boss::concurrency::UniqueLock const lock(coordination.engineStateMutex);
        if(coordination.librariesAwaitingTeardown.empty()) {
          coordination.reconfigurationInProgress = false;
          return;
        }
        librariesToUnload.swap(coordination.librariesAwaitingTeardown);
      }
      try {
        unloadLibraries(librariesToUnload);
      } catch(...) { // NOLINT(bugprone-empty-catch)
      }
    }
  }

  /// Queues the libraries while a dispatch is in flight, because a destructor can neither refuse
  /// to run nor wait.
  static void tearDownOnceNoDispatchIsInFlight(EngineDispatchCoordination& coordination,
                                               vector<LibraryAndFunctions> detachedLibraries)
      BOSS_EXCLUDES(coordination.engineStateMutex) {
    if(detachedLibraries.empty()) {
      return;
    }
    {
      boss::concurrency::UniqueLock const lock(coordination.engineStateMutex);
      coordination.librariesAwaitingTeardown.insert(coordination.librariesAwaitingTeardown.end(),
                                                    make_move_iterator(detachedLibraries.begin()),
                                                    make_move_iterator(detachedLibraries.end()));
      bool const anotherThreadTearsDown = coordination.reconfigurationInProgress.load() ||
                                          coordination.evaluationsInFlight.load() > 0;
      coordination.reconfigurationInProgress = true;
      if(anotherThreadTearsDown) {
        return;
      }
    }
    finishReconfiguration(coordination);
  }

  /// Calls finishReconfiguration in its destructor. The reconfiguration operators declare the
  /// object so that its destructor runs after unloadLibraries has returned or thrown.
  class ReconfigurationEnd {
  public:
    ReconfigurationEnd() = default;
    ~ReconfigurationEnd() { finishReconfiguration(engineDispatchCoordination()); }
    ReconfigurationEnd(ReconfigurationEnd const&) = delete;
    ReconfigurationEnd(ReconfigurationEnd&&) = delete;
    ReconfigurationEnd& operator=(ReconfigurationEnd const&) = delete;
    ReconfigurationEnd& operator=(ReconfigurationEnd&&) = delete;
  };

  static string symbolToLibraryName(Symbol const& engine) {
#ifdef _WIN32
    return engine.getName() + "Engine.dll";
#else
    return "lib" + engine.getName() + "Engine.so";
#endif
  }

  template <typename EnginePath> static string libraryNameForEnginePath(EnginePath const& path) {
    auto resolve =
        overload([](Symbol const& engine) -> string { return symbolToLibraryName(engine); },
                 [](ComplexExpression const& engine) -> string {
                   return symbolToLibraryName(engine.getHead());
                 },
                 [&path](auto const& /*unused*/) -> string { return get<string>(path); });
    return visit(resolve, path);
  }

  /// Names an engine after its library file: the file name without directory and extension,
  /// and without a leading "lib".
  static string engineNameForLibrary(string const& libraryName) {
    auto engineName = path(libraryName).stem().string();
    if(engineName.compare(0, 3, "lib") == 0) {
      engineName.erase(0, 3);
    }
    return engineName;
  }

  struct LibraryCache : private unordered_map<string, LibraryAndFunctions> {
    /// Look up an already-loaded library. Caller holds a shared lock on engineStateMutex.
    ::std::optional<LibraryAndFunctions> tryGet(string const& libraryPath) const {
      auto const it = find(libraryPath);
      if(it == end()) {
        return ::std::nullopt;
      }
      return it->second;
    }

    /// dlopen the library if absent, then return it. Caller holds an EXCLUSIVE lock.
    LibraryAndFunctions const& loadOrGet(string const& libraryPath) {
      if(count(libraryPath) == 0) {
        const auto* n = libraryPath.c_str();
        if(auto* library = dlopen(n, RTLD_NOW | RTLD_NODELETE)) { // NOLINT(hicpp-signed-bitwise)
          if(auto* evalSym = dlsym(library, "evaluate")) {
            emplace(libraryPath,
                    LibraryAndFunctions {
                        library, reinterpret_cast<void (*)(void)>(dlsym(library, "reset")),
                        reinterpret_cast<LibraryAndFunctions::EntryPoint>(evalSym)});
          } else {
            throw runtime_error("library \"" + libraryPath +
                                "\" does not provide an evaluate function: " + dlerror());
          }
        } else {
          throw runtime_error("library \"" + libraryPath + "\" could not be loaded: " + dlerror());
        }
      };
      return unordered_map::at(libraryPath);
    }

    /// Removes and returns every entry, so that the caller can unload the libraries after
    /// releasing the lock.
    vector<LibraryAndFunctions> detachAll() {
      vector<LibraryAndFunctions> libs;
      libs.reserve(size());
      for(const auto& [name, library] : *this) {
        libs.push_back(library);
      }
      unordered_map<string, LibraryAndFunctions>::clear();
      return libs;
    }

    ~LibraryCache() { tearDownOnceNoDispatchIsInFlight(engineDispatchCoordination(), detachAll()); }

    LibraryCache() = default;
    LibraryCache(LibraryCache const&) = delete;
    LibraryCache(LibraryCache&&) = delete;
    LibraryCache& operator=(LibraryCache const&) = delete;
    LibraryCache& operator=(LibraryCache&&) = delete;
  } libraries BOSS_GUARDED_BY(engineStateMutex);

  vector<Expression> defaultEnginePipeline BOSS_GUARDED_BY(engineStateMutex);

  /// Returns an engine's evaluate function, loading the engine on first use. Takes
  /// engineStateMutex shared to find a loaded library, and releases it and takes it
  /// exclusively only to load a missing library. Callers must not hold engineStateMutex.
  LibraryAndFunctions::EntryPoint resolveEvaluateFunction(string const& libraryPath)
      BOSS_EXCLUDES(engineStateMutex) {
    {
      boss::concurrency::SharedLock const lock(engineStateMutex);
      if(auto const entry = libraries.tryGet(libraryPath)) {
        return entry->evaluateFunction;
      }
    }
    boss::concurrency::UniqueLock const lock(engineStateMutex);
    return libraries.loadOrGet(libraryPath).evaluateFunction;
  }

  vector<Expression> copyDefaultEnginePipeline() BOSS_EXCLUDES(engineStateMutex) {
    boss::concurrency::SharedLock const lock(engineStateMutex);
    vector<Expression> pipeline;
    pipeline.reserve(defaultEnginePipeline.size());
    for(auto const& entry : defaultEnginePipeline) {
      pipeline.push_back(entry.clone(boss::expressions::CloneReason::EVALUATE_CONST_EXPRESSION));
    }
    return pipeline;
  }

  using ExpressionHandle = std::unique_ptr<BOSSExpression, decltype(&freeBOSSExpression)>;

  struct AnswerOrFailureReport : ExpressionHandle {
    bool isFailureReport;
  };

  static ComplexExpression headWithEllipsis(ComplexExpression const& expression) {
    return expression.getArguments().empty()
               ? ComplexExpression(Symbol(expression.getHead()), ExpressionArguments())
               : ComplexExpression(Symbol(expression.getHead()),
                                   ExpressionArguments(Symbol("...")));
  }

  static Expression inputForErrorReport(BOSSExpression const* input) {
    if(input == nullptr) {
      return Symbol("Nothing");
    }
    return visit(overload([](ComplexExpression const& complex)
                              -> Expression { return headWithEllipsis(complex); },
                          [](Symbol const& symbol) -> Expression { return symbol; },
                          [](auto const& /*unused*/) -> Expression { return Symbol("..."); }),
                 input->delegate);
  }

  template <typename EnginePath>
  static Expression pipelineEntryForErrorReport(EnginePath const& engine) {
    return visit(
        overload([](ComplexExpression const& spec) -> Expression { return headWithEllipsis(spec); },
                 [](Symbol const& symbol) -> Expression { return symbol; },
                 [](string const& libraryPath) -> Expression { return libraryPath; },
                 [](auto const& /*unused*/) -> Expression { return Symbol("..."); }),
        engine);
  }

  static Expression reportEngineFailure(vector<Expression> const& pipelineEntriesForErrorReport,
                                        std::size_t engineThatThrew, Expression&& reportedInput,
                                        string message) {
    ExpressionArguments remainingEngines;
    for(auto engine = engineThatThrew; engine < pipelineEntriesForErrorReport.size(); ++engine) {
      remainingEngines.push_back(pipelineEntriesForErrorReport.at(engine).clone(
          boss::expressions::CloneReason::EXPRESSION_WRAPPING));
    }
    return "ErrorWhenEvaluatingExpression"_(
        "EvaluateInEngines"_("List"_(std::move(remainingEngines)), std::move(reportedInput)),
        Expression(std::move(message)));
  }

  static AnswerOrFailureReport
  pipeThroughEngines(vector<LibraryAndFunctions::EntryPoint> const& entryPoints,
                     vector<Expression> const& pipelineEntriesForErrorReport,
                     ExpressionHandle input) {
    auto current = std::move(input);
    for(std::size_t engine = 0; engine < entryPoints.size(); ++engine) {
      auto reportedInput = inputForErrorReport(current.get());
      try {
        current = ExpressionHandle(entryPoints.at(engine)(current.get()), &freeBOSSExpression);
      } catch(std::exception const& exception) {
        return {ExpressionHandle(new BOSSExpression {reportEngineFailure(
                                     pipelineEntriesForErrorReport, engine,
                                     std::move(reportedInput), exception.what())},
                                 &freeBOSSExpression),
                true};
      }
    }
    return {std::move(current), false};
  }

  /// EnginePath must be non-const: a const spec misses the ComplexExpression& overload and silently
  /// keeps evaluate.
  template <typename EnginePath>
  LibraryAndFunctions::EntryPoint resolveEngineEntryPoint(EnginePath& enginePath)

      BOSS_EXCLUDES(engineStateMutex) {
    auto* entryPoint = resolveEvaluateFunction(libraryNameForEnginePath(enginePath));
    visit(overload(
              [&entryPoint](ComplexExpression& engine) {
                auto [_originalHead, statics, dynamics, spans] = std::move(engine).decompose();
                // decltype(statics) {}: moving the empty tuple fails clang-tidy's
                // move-without-effect check.
                auto const input =
                    ExpressionHandle(new BOSSExpression {ComplexExpression(
                                         Symbol("GetEntryPoint"), decltype(statics) {},
                                         std::move(dynamics), std::move(spans))},
                                     &freeBOSSExpression);
                auto const output = ExpressionHandle(entryPoint(input.get()), &freeBOSSExpression);
                if(output != nullptr) {
                  visit(overload(
                            [&entryPoint](std::int64_t value) {
                              if(value == 0) {
                                return;
                              }
                              auto const asPointerInt = static_cast<std::intptr_t>(value);
                              if(static_cast<std::int64_t>(asPointerInt) != value) {
                                throw std::runtime_error(
                                    "GetEntryPoint returned " + std::to_string(value) +
                                    ", which does not fit in a pointer on this platform");
                              }
                              // NOLINTBEGIN(performance-no-int-to-ptr)
                              entryPoint =
                                  reinterpret_cast<LibraryAndFunctions::EntryPoint>(asPointerInt);
                              // NOLINTEND(performance-no-int-to-ptr)
                            },
                            [](auto const& /*unused*/) {}),
                        output->delegate);
                }
              },
              [](auto const& /*unused*/) {}),
          enginePath);
    return entryPoint;
  }

  /// Formats one engine's section of an engine description document: a heading with the
  /// engine's name, then the engine's description or the reason why there is none.
  static string engineDescriptionSection(string const& engineName,
                                         AnswerOrFailureReport const& answer) {
    auto const heading = "## " + engineName + "\n\n";
    if(answer == nullptr) {
      return heading + "(no description: the engine returned nothing)\n";
    }
    if(answer.isFailureReport) {
      std::stringstream report;
      report << answer->delegate;
      return heading + "(no description: " + report.str() + ")\n";
    }
    return heading + visit(overload([](string const& description) { return description; },
                                    [](auto const& unexpected) {
                                      std::stringstream rendered;
                                      rendered << unexpected;
                                      return "(no description: the engine returned " +
                                             rendered.str() + ")\n";
                                    }),
                           answer->delegate);
  }

  unordered_map<Symbol, std::function<Expression(ComplexExpression&&)>> const registeredOperators {
      {Symbol("EvaluateInEngines"),
       [this](auto&& e) -> Expression {
         InFlightGuard const inFlight(evaluationsInFlight, reconfigurationInProgress,
                                      engineStateMutex);
         auto entryPoints = vector<LibraryAndFunctions::EntryPoint>();
         auto pipelineEntriesForErrorReport = vector<Expression>();
         auto args = get<ComplexExpression>(e.getArguments().at(0)).getArguments();
         // self = this: clang 18 asserts in Sema::BuildCaptureInit when a lambda nested in a
         // generic lambda captures this.
         for_each(args.begin(), args.end(),
                  [self = this, &entryPoints, &pipelineEntriesForErrorReport](auto&& enginePath) {
                    pipelineEntriesForErrorReport.push_back(
                        pipelineEntryForErrorReport(enginePath));
                    entryPoints.push_back(self->resolveEngineEntryPoint(enginePath));
                  });
         auto const dispatch = [&entryPoints, &pipelineEntriesForErrorReport](auto&& argument) {
           return pipeThroughEngines(
               entryPoints, pipelineEntriesForErrorReport,
               ExpressionHandle(new BOSSExpression {std::forward<decltype(argument)>(argument)},
                                &freeBOSSExpression));
         };
         auto arguments = e.getArguments();
         for(auto argument = std::next(arguments.begin()); argument != prev(arguments.end());
             ++argument) {
           auto evaluation = dispatch(*argument);
           if(evaluation.isFailureReport) {
             return std::move(evaluation->delegate);
           }
         }
         auto answer = dispatch(*prev(arguments.end()));
         if(answer == nullptr) {
           return Symbol("Nothing");
         }
         return std::move(answer->delegate);
       }},
      {Symbol("SetDefaultEnginePipeline"),
       [this](auto&& expression) -> Expression {
         // Builds the new pipeline in a local vector, without the lock. Validation throws before
         // this operator changes shared state, so a bad argument leaves the pipeline unchanged.
         vector<Expression> newPipeline;
         algorithm::visitEach(expression.getArguments(), [&newPipeline](auto&& engine) {
           using EngineArg = std::decay_t<decltype(engine)>;
           if constexpr(std::is_same_v<EngineArg, string> || std::is_same_v<EngineArg, Symbol>) {
             newPipeline.push_back(engine);
           } else if constexpr(std::is_same_v<EngineArg, ComplexExpression>) {
             newPipeline.push_back(
                 engine.clone(boss::expressions::CloneReason::EVALUATE_CONST_EXPRESSION));
           } else {
             std::stringstream errorMessage;
             errorMessage << "SetDefaultEnginePipeline received unsupported argument: " << engine;
             throw std::runtime_error(errorMessage.str());
           }
         });
         vector<LibraryAndFunctions> toUnload;
         // Declared outside the lock scope, so that its destructor clears the flag on every exit,
         // after the lock has been released.
         std::optional<ReconfigurationEnd> endReconfiguration;
         {
           boss::concurrency::UniqueLock const lock(engineStateMutex);
           beginReconfiguration("SetDefaultEnginePipeline");
           endReconfiguration.emplace();
           defaultEnginePipeline = std::move(newPipeline);
           toUnload = libraries.detachAll();
         }
         unloadLibraries(toUnload);
         return "okay";
       }},
      {Symbol("GetDefaultEnginePipeline"),
       [this](auto&& /*expression*/) -> Expression {
         boss::concurrency::SharedLock const lock(engineStateMutex);
         ExpressionArguments args;
         args.reserve(defaultEnginePipeline.size());
         for(auto const& entry : defaultEnginePipeline) {
           args.push_back(entry.clone(boss::expressions::CloneReason::EVALUATE_CONST_EXPRESSION));
         }
         return "List"_(std::move(args));
       }},
      {Symbol("GetEngineDescription"),
       [this](auto&& /*expression*/) -> Expression {
         using boss::utilities::operator""_;
         InFlightGuard const inFlight(evaluationsInFlight, reconfigurationInProgress,
                                      engineStateMutex);
         auto pipeline = copyDefaultEnginePipeline();
         string descriptions;
         for(auto& enginePath : pipeline) {
           auto const libraryName = libraryNameForEnginePath(enginePath);
           vector<Expression> pipelineEntriesForErrorReport;
           pipelineEntriesForErrorReport.push_back(pipelineEntryForErrorReport(enginePath));
           AnswerOrFailureReport evaluation {ExpressionHandle(nullptr, &freeBOSSExpression), false};
           try {
             evaluation = pipeThroughEngines(
                 {resolveEngineEntryPoint(enginePath)}, pipelineEntriesForErrorReport,
                 ExpressionHandle(new BOSSExpression {"GetEngineDescription"_()},
                                  &freeBOSSExpression));
           } catch(std::exception const& exception) {
             evaluation = {ExpressionHandle(new BOSSExpression {reportEngineFailure(
                                                pipelineEntriesForErrorReport, 0,
                                                "GetEngineDescription"_(), exception.what())},
                                            &freeBOSSExpression),
                           true};
           }
           if(!descriptions.empty()) {
             descriptions += "\n";
           }
           descriptions += engineDescriptionSection(engineNameForLibrary(libraryName), evaluation);
         }
         return descriptions;
       }},
      {Symbol("ResetEngines"), [this](auto&& /*expression*/) -> Expression {
         vector<LibraryAndFunctions> toUnload;
         // Declared outside the lock scope, so that its destructor clears the flag on every exit,
         // after the lock has been released.
         std::optional<ReconfigurationEnd> endReconfiguration;
         {
           boss::concurrency::UniqueLock const lock(engineStateMutex);
           beginReconfiguration("ResetEngines");
           endReconfiguration.emplace();
           defaultEnginePipeline.clear();
           toUnload = libraries.detachAll();
         }
         unloadLibraries(toUnload);
         return "okay";
       }}};

  bool isBootstrapCommand(Expression const& expression) {
    return visit(utilities::overload(
                     [this](ComplexExpression const& expression) {
                       return registeredOperators.count(expression.getHead()) > 0;
                     },
                     [](auto const& /* unused */
                     ) { return false; }),
                 expression);
  }

  /// Below the root, GetEngineDescription is a request for the engines of an enclosing
  /// EvaluateInEngines.
  bool isLeftUnevaluated(ComplexExpression const& expression, bool isRootExpression) const {
    return registeredOperators.count(expression.getHead()) == 0 ||
           (!isRootExpression && expression.getHead() == Symbol("GetEngineDescription"));
  }

public:
  BootstrapEngine() {
#ifdef BOSS_DEFAULT_ENGINE_LIBS
    // BOSS_DEFAULT_ENGINE_LIBS expands to comma-separated string literals, so iterate them as
    // pointers and store each as a string entry, the form SetDefaultEnginePipeline uses for a path.
    for(auto const* library : {BOSS_DEFAULT_ENGINE_LIBS}) {
      defaultEnginePipeline.emplace_back(library);
    }
#endif
  }
  ~BootstrapEngine() = default;
  BootstrapEngine(BootstrapEngine const&) = delete;
  BootstrapEngine(BootstrapEngine&&) = delete;
  BootstrapEngine& operator=(BootstrapEngine const&) = delete;
  BootstrapEngine& operator=(BootstrapEngine&&) = delete;

  auto evaluateArguments(ComplexExpression&& expr) {
    std::transform(make_move_iterator(begin(expr.getArguments())),
                   make_move_iterator(end(expr.getArguments())), begin(expr.getArguments()),
                   [this](auto&& e) { return evaluate(std::forward<decltype(e)>(e), false); });
    return std::move(expr);
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  Expression evaluate(Expression&& e, bool isRootExpression = true) {
    using boss::utilities::operator""_;

    // Copies the engine pipeline under the shared lock, but only for a root expression that is
    // not a bootstrap command: evaluations of arguments and bootstrap commands do not go
    // through the pipeline. This function releases the lock before the dispatch, so
    // BootstrapEngine holds no lock during an engine call.
    std::optional<InFlightGuard> rootDispatch;
    ExpressionArguments pipelineArgs;
    if(isRootExpression && !isBootstrapCommand(e)) {
      boss::concurrency::SharedLock const lock(engineStateMutex);
      if(!defaultEnginePipeline.empty()) {
        // This evaluation counts as in flight before the lock is released, so no reconfiguration
        // can run between copying the pipeline and dispatching through the copy.
        rootDispatch.emplace(evaluationsInFlight, reconfigurationInProgress, engineStateMutex,
                             InFlightGuard::SharedLockHeld {});
      }
      pipelineArgs.reserve(defaultEnginePipeline.size());
      for(auto const& entry : defaultEnginePipeline) {
        pipelineArgs.push_back(
            entry.clone(boss::expressions::CloneReason::EVALUATE_CONST_EXPRESSION));
      }
    }
    auto wrappedE = !pipelineArgs.empty()
                        ? "EvaluateInEngines"_("List"_(std::move(pipelineArgs)), std::move(e))
                        : std::move(e);
    return visit(overload(
                     [this, isRootExpression](ComplexExpression&& unevaluatedE) -> Expression {
                       if(isLeftUnevaluated(unevaluatedE, isRootExpression)) {
                         return std::move(unevaluatedE);
                       }
                       auto const& op = registeredOperators.at(unevaluatedE.getHead());
                       return op(evaluateArguments(std::move(unevaluatedE)));
                     },
                     [](auto&& e) -> Expression { return e; }),
                 std::forward<Expression>(wrappedE));
  }
};
} // namespace
} // namespace engines

} // namespace boss
