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

struct LibraryAndFunctions {
  using EntryPoint = BOSSExpression* (*)(BOSSExpression*);
  void* library;
  void (*resetFunction)(void);
  EntryPoint evaluateFunction;
};

/// Callers must not hold engineStateMutex, because reset() is an engine call. A reset() that
/// throws does not stop the remaining libraries from being reset and unloaded; the first
/// exception is rethrown once every library has been unloaded.
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

  /// BootstrapEngine reads `libraries`, `defaultEnginePipeline` and `librariesAwaitingTeardown`
  /// only while it holds this mutex, shared or exclusively, and changes them only while it
  /// holds the mutex exclusively. Declared first so that the BOSS_GUARDED_BY annotations below
  /// can name it. The mutex and the two atomics below are static, so every BootstrapEngine in
  /// the process uses the same ones: dlopen returns the same engine image to every instance,
  /// and with separate state one instance's ResetEngines could reset an engine during another
  /// instance's dispatch.
  static inline boss::concurrency::SharedMutex engineStateMutex;

  /// The number of dispatches in flight. Engine calls run without engineStateMutex, so the
  /// mutex alone cannot stop a reconfiguration from resetting or unloading an engine during a
  /// call. beginReconfiguration therefore throws while this count is not 0.
  static inline std::atomic<std::size_t> evaluationsInFlight {0};

  /// True from the moment a reconfiguration takes the exclusive lock until the reconfiguration
  /// has reset and unloaded its engines. The reconfiguration releases the lock before
  /// unloadLibraries runs, and a dispatch that started in between would load an engine again
  /// and call it while unloadLibraries is still running that engine's reset().
  static inline std::atomic<bool> reconfigurationInProgress {false};

  /// Libraries of destroyed BootstrapEngine instances, to be reset and unloaded once no
  /// dispatch is in flight. finishReconfiguration resets and unloads these libraries before
  /// it clears reconfigurationInProgress.
  static inline vector<LibraryAndFunctions>
      librariesAwaitingTeardown BOSS_GUARDED_BY(engineStateMutex);

  /// Increments evaluationsInFlight for the lifetime of the object.
  ///
  /// The constructor increments the count under the shared lock, and beginReconfiguration()
  /// reads the count under the exclusive lock. Without the shared lock, the count could go
  /// from 0 to 1 after a reconfiguration had read 0 and before the reconfiguration changed
  /// engine state, and the reconfiguration would then reset() and dlclose() engines during a
  /// dispatch that had just started. With the shared lock, a reconfiguration that holds the
  /// exclusive lock and reads 0 can rely on no dispatch running and none starting until the
  /// reconfiguration releases the lock. The constructor releases the shared lock before the
  /// engine call, so BootstrapEngine still holds no lock during an engine call.
  ///
  /// The destructor decrements the count under the shared lock as well, so the decrement
  /// cannot interleave with tearDownOnceNoDispatchIsInFlight. No reconfiguration starts while
  /// a dispatch is in flight, so a count that reaches 0 while reconfigurationInProgress is set
  /// means that a destroyed instance queued its libraries behind the in-flight dispatches. The
  /// destructor that brings the count to 0 resets and unloads those libraries.
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
        finishReconfiguration();
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

  /// Resets and unloads queued libraries until none remain, then clears
  /// reconfigurationInProgress. One thread at a time runs this function: the reconfiguration
  /// operator or destructor that set reconfigurationInProgress, or the last dispatch that a
  /// queued library waited for. The loop also resets and unloads libraries that another
  /// destroyed instance queued while this thread was unloading.
  /// It does not throw, so that a destructor can call it. A queued library's reset() that
  /// throws has no caller to report to, and unloadLibraries has unloaded every library before
  /// it rethrows, so the loop discards that exception.
  static void finishReconfiguration() BOSS_EXCLUDES(engineStateMutex) {
    while(true) {
      vector<LibraryAndFunctions> librariesToUnload;
      {
        boss::concurrency::UniqueLock const lock(engineStateMutex);
        if(librariesAwaitingTeardown.empty()) {
          reconfigurationInProgress = false;
          return;
        }
        librariesToUnload.swap(librariesAwaitingTeardown);
      }
      try {
        unloadLibraries(librariesToUnload);
      } catch(...) { // NOLINT(bugprone-empty-catch)
      }
    }
  }

  /// A destructor cannot refuse to run the way the reconfiguration operators do, and waiting
  /// for in-flight dispatches would deadlock when one of those dispatches destroys this
  /// instance. So while a dispatch is in flight, or another thread is resetting and unloading
  /// engines, this function queues the libraries, sets reconfigurationInProgress and returns.
  /// While reconfigurationInProgress is set, InFlightGuard refuses new dispatches.
  static void tearDownOnceNoDispatchIsInFlight(vector<LibraryAndFunctions> detachedLibraries)
      BOSS_EXCLUDES(engineStateMutex) {
    if(detachedLibraries.empty()) {
      return;
    }
    {
      boss::concurrency::UniqueLock const lock(engineStateMutex);
      librariesAwaitingTeardown.insert(librariesAwaitingTeardown.end(),
                                       make_move_iterator(detachedLibraries.begin()),
                                       make_move_iterator(detachedLibraries.end()));
      bool const anotherThreadTearsDown =
          reconfigurationInProgress.load() || evaluationsInFlight.load() > 0;
      reconfigurationInProgress = true;
      if(anotherThreadTearsDown) {
        return;
      }
    }
    finishReconfiguration();
  }

  /// Calls finishReconfiguration in its destructor. The reconfiguration operators declare the
  /// object so that its destructor runs after unloadLibraries has returned or thrown.
  class ReconfigurationEnd {
  public:
    ReconfigurationEnd() = default;
    ~ReconfigurationEnd() { finishReconfiguration(); }
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

  /// Resolves a pipeline entry to the library it names: a Symbol, and a ComplexExpression via its
  /// head, map to the platform library name. Any other entry has to be a string, and
  /// libraryNameForEnginePath uses that string verbatim. For an entry that is none of these,
  /// get<string> throws ArgumentTypeMismatch<string>, a subclass of bad_variant_access.
  ///
  /// libraryNameForEnginePath is a template so that callers can pass both an Expression and an
  /// ArgumentWrapper.
  template <typename EnginePath> static string libraryNameForEnginePath(EnginePath const& path) {
    auto resolve =
        overload([](Symbol const& engine) -> string { return symbolToLibraryName(engine); },
                 [](ComplexExpression const& engine) -> string {
                   return symbolToLibraryName(engine.getHead());
                 },
                 [&path](auto const& /*unused*/) -> string { return get<string>(path); });
    return visit(resolve, path);
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

    ~LibraryCache() { tearDownOnceNoDispatchIsInFlight(detachAll()); }

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

  using ExpressionHandle = std::unique_ptr<BOSSExpression, decltype(&freeBOSSExpression)>;

  struct EngineAnswer {
    ExpressionHandle answer;
    bool engineThrew;
  };

  static ComplexExpression elideArguments(ComplexExpression const& expression) {
    ExpressionArguments arguments;
    if(!expression.getArguments().empty()) {
      arguments.emplace_back(Symbol("..."));
    }
    return ComplexExpression(Symbol(expression.getHead()), std::move(arguments));
  }

  static Expression summarizeInput(Expression const& input) {
    return visit(
        overload(
            [](ComplexExpression const& complex) -> Expression { return elideArguments(complex); },
            [](Symbol const& symbol) -> Expression { return symbol; },
            [](auto const& /*unused*/) -> Expression { return Symbol("..."); }),
        input);
  }

  template <typename EnginePath> static Expression summarizeEngine(EnginePath const& engine) {
    return visit(
        overload([](ComplexExpression const& spec) -> Expression { return elideArguments(spec); },
                 [](Symbol const& symbol) -> Expression { return symbol; },
                 [](string const& libraryPath) -> Expression { return libraryPath; },
                 [](auto const& /*unused*/) -> Expression { return Symbol("..."); }),
        engine);
  }

  /// Reports only a summary of the input, because the engine may have consumed its input before
  /// it threw, and copying every input in advance would copy expressions that can be very large.
  static Expression reportEngineFailure(vector<Expression> const& engineSummaries,
                                        std::size_t engineThatThrew, Expression&& inputSummary,
                                        string message) {
    ExpressionArguments remainingEngines;
    for(auto engine = engineThatThrew; engine < engineSummaries.size(); ++engine) {
      remainingEngines.push_back(
          engineSummaries.at(engine).clone(boss::expressions::CloneReason::EXPRESSION_WRAPPING));
    }
    return "ErrorWhenEvaluatingExpression"_(
        "EvaluateInEngines"_("List"_(std::move(remainingEngines)), std::move(inputSummary)),
        Expression(std::move(message)));
  }

  static EngineAnswer
  dispatchThroughEngines(vector<LibraryAndFunctions::EntryPoint> const& entryPoints,
                         vector<Expression> const& engineSummaries, ExpressionHandle input) {
    auto current = std::move(input);
    for(std::size_t engine = 0; engine < entryPoints.size() && current != nullptr; ++engine) {
      auto inputSummary = summarizeInput(current->delegate);
      try {
        current = ExpressionHandle(entryPoints.at(engine)(current.get()), &freeBOSSExpression);
      } catch(std::exception const& exception) {
        return {ExpressionHandle(new BOSSExpression {reportEngineFailure(engineSummaries, engine,
                                                                         std::move(inputSummary),
                                                                         exception.what())},
                                 &freeBOSSExpression),
                true};
      }
    }
    return {std::move(current), false};
  }

  /// Moves a complex spec's arguments into the GetEntryPoint request, so the caller must not read
  /// the spec afterwards. EnginePath has to be non-const: for a const spec, the
  /// ComplexExpression& overload does not match, and the fallback silently keeps evaluate.
  template <typename EnginePath>
  LibraryAndFunctions::EntryPoint resolveEngineEntryPoint(EnginePath& enginePath)
      BOSS_EXCLUDES(engineStateMutex) {
    // resolveEvaluateFunction takes engineStateMutex and releases the mutex before it
    // returns, so BootstrapEngine holds no lock during the GetEntryPoint call below.
    auto* entryPoint = resolveEvaluateFunction(libraryNameForEnginePath(enginePath));
    visit(overload(
              [&entryPoint](ComplexExpression& engine) {
                auto [_originalHead, statics, dynamics, spans] = std::move(engine).decompose();
                // ExpressionHandle frees the GetEntryPoint request and the engine's answer,
                // also when the engine call or the decoding of the answer throws.
                // Passes a new empty static-argument tuple instead of moving `statics`.
                // `statics` is an empty std::tuple<>, so clang-tidy reports a move of
                // `statics` as a move without effect, and CI treats that warning as an
                // error. decltype(statics) also keeps the structured binding `statics` in
                // use.
                auto const input =
                    ExpressionHandle(new BOSSExpression {ComplexExpression(
                                         Symbol("GetEntryPoint"), decltype(statics) {},
                                         std::move(dynamics), std::move(spans))},
                                     &freeBOSSExpression);
                auto const output = ExpressionHandle(entryPoint(input.get()), &freeBOSSExpression);
                if(output != nullptr) {
                  visit(overload(
                            [&entryPoint](std::int64_t value) {
                              // An answer of 0 keeps evaluate as the entry point, so that
                              // BootstrapEngine does not call a null function pointer.
                              if(value == 0) {
                                return;
                              }
                              // Where intptr_t is narrower than int64_t (32-bit builds), the
                              // cast below drops the high bits, and BootstrapEngine would
                              // call a wrong address. BootstrapEngine therefore throws for a
                              // value that the cast changes.
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
                            // An answer that is not an int64 keeps evaluate as the entry
                            // point, so that an engine without a GetEntryPoint
                            // implementation still evaluates expressions.
                            [](auto const& /*unused*/) {}),
                        output->delegate);
                }
              },
              [](auto const& /*unused*/) {}),
          enginePath);
    return entryPoint;
  }

  unordered_map<Symbol, std::function<Expression(ComplexExpression&&)>> const registeredOperators {
      {Symbol("EvaluateInEngines"),
       [this](auto&& e) -> Expression {
         // Counts this evaluation as in flight, so that a concurrent SetDefaultEnginePipeline or
         // ResetEngines throws instead of unloading engines that this evaluation calls.
         InFlightGuard const inFlight(evaluationsInFlight, reconfigurationInProgress,
                                      engineStateMutex);
         auto entryPoints = vector<LibraryAndFunctions::EntryPoint>();
         auto engineSummaries = vector<Expression>();
         auto args = get<ComplexExpression>(e.getArguments().at(0)).getArguments();
         // Looks up every engine's entry point before the first engine call, so that the
         // dispatch loop below runs without engineStateMutex. Captures `this` as `self`, because
         // clang 18 fails an assertion in Sema::BuildCaptureInit when a lambda nested in a generic
         // lambda captures `this` directly.
         for_each(args.begin(), args.end(),
                  [self = this, &entryPoints, &engineSummaries](auto&& enginePath) {
                    engineSummaries.push_back(summarizeEngine(enginePath));
                    entryPoints.push_back(self->resolveEngineEntryPoint(enginePath));
                  });
         auto const dispatch = [&entryPoints, &engineSummaries](auto&& argument) {
           return dispatchThroughEngines(
               entryPoints, engineSummaries,
               ExpressionHandle(new BOSSExpression {std::forward<decltype(argument)>(argument)},
                                &freeBOSSExpression));
         };
         // The first argument is the engine list.
         auto arguments = e.getArguments();
         for(auto argument = std::next(arguments.begin()); argument != prev(arguments.end());
             ++argument) {
           auto evaluation = dispatch(*argument);
           if(evaluation.engineThrew) {
             return std::move(evaluation.answer->delegate);
           }
         }
         return std::move(dispatch(*prev(arguments.end())).answer->delegate);
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
         // Under the exclusive lock, this operator replaces the pipeline and removes the loaded
         // libraries from the cache. It unloads those libraries after releasing the lock, because
         // reset() is an engine call.
         //
         // The in-flight check runs under the same exclusive lock as these changes, because a
         // dispatch could otherwise start between the check and the changes.
         //
         // This operator resets and unloads every loaded engine, including the engines that remain
         // in the pipeline, because a new pipeline starts a new configuration. The next evaluation
         // therefore loads every engine again, even when the caller only reordered the pipeline.
         // ResetEngines differs only in also clearing the pipeline.
         vector<LibraryAndFunctions> toUnload;
         // Emplaced right after beginReconfiguration but declared out here, so that its destructor
         // clears the flag on every exit, a throwing detachAll included, after the lock below has
         // been released.
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
         // InFlightGuard counts this operator as an in-flight dispatch, because the operator calls
         // engines. Without that count, beginReconfiguration would accept a concurrent
         // SetDefaultEnginePipeline or ResetEngines, which could unload an engine during its
         // GetEngineDescription call.
         InFlightGuard const inFlight(evaluationsInFlight, reconfigurationInProgress,
                                      engineStateMutex);
         // Copies the pipeline under a shared lock, so that the engine calls below run without
         // engineStateMutex.
         vector<Expression> pipeline;
         {
           boss::concurrency::SharedLock const lock(engineStateMutex);
           pipeline.reserve(defaultEnginePipeline.size());
           for(auto const& entry : defaultEnginePipeline) {
             pipeline.push_back(
                 entry.clone(boss::expressions::CloneReason::EVALUATE_CONST_EXPRESSION));
           }
         }
         string descriptions;
         for(auto& enginePath : pipeline) {
           // The section heading uses the same libraryName, so that the heading names the library
           // that resolveEvaluateFunction loaded.
           auto const libraryName = libraryNameForEnginePath(enginePath);
           vector<Expression> engineSummaries;
           engineSummaries.push_back(summarizeEngine(enginePath));
           EngineAnswer evaluation {ExpressionHandle(nullptr, &freeBOSSExpression), false};
           try {
             evaluation = dispatchThroughEngines(
                 {resolveEngineEntryPoint(enginePath)}, engineSummaries,
                 ExpressionHandle(new BOSSExpression {"GetEngineDescription"_()},
                                  &freeBOSSExpression));
           } catch(std::exception const& exception) {
             evaluation = {ExpressionHandle(new BOSSExpression {reportEngineFailure(
                                                engineSummaries, 0, "GetEngineDescription"_(),
                                                exception.what())},
                                            &freeBOSSExpression),
                           true};
           }
           auto engineName = path(libraryName).stem().string();
           if(engineName.compare(0, 3, "lib") == 0) {
             engineName.erase(0, 3);
           }
           // Writes a section for every engine, also for an engine that answers with nothing, with
           // a non-string or with an exception, so that the document lists every engine in the
           // pipeline.
           if(!descriptions.empty()) {
             descriptions += "\n";
           }
           descriptions += "## " + engineName + "\n\n";
           if(evaluation.answer == nullptr) {
             descriptions += "(no description: the engine returned nothing)\n";
             continue;
           }
           if(evaluation.engineThrew) {
             std::stringstream report;
             report << evaluation.answer->delegate;
             descriptions += "(no description: " + report.str() + ")\n";
             continue;
           }
           visit(
               overload([&descriptions](string const& description) { descriptions += description; },
                        [&descriptions](auto const& unexpected) {
                          std::stringstream rendered;
                          rendered << unexpected;
                          descriptions +=
                              "(no description: the engine returned " + rendered.str() + ")\n";
                        }),
               evaluation.answer->delegate);
         }
         return descriptions;
       }},
      {Symbol("ResetEngines"), [this](auto&& /*expression*/) -> Expression {
         // Clears the pipeline and the library cache under the exclusive lock, and resets and
         // unloads the engines after releasing the lock, because reset() is an engine call. The
         // in-flight check runs under the exclusive lock for the same reason as in
         // SetDefaultEnginePipeline.
         vector<LibraryAndFunctions> toUnload;
         // Emplaced right after beginReconfiguration but declared out here, so that its destructor
         // clears the flag on every exit, a throwing detachAll included, after the lock below has
         // been released.
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
    return visit(
        overload(
            [this, isRootExpression](ComplexExpression&& unevaluatedE) -> Expression {
              // Below the root, GetEngineDescription goes to the engines, which answer it
              // themselves.
              if(registeredOperators.count(unevaluatedE.getHead()) == 0 ||
                 (!isRootExpression && unevaluatedE.getHead() == Symbol("GetEngineDescription"))) {
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
