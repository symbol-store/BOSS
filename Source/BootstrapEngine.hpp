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
#include <cstdint>
#include <iterator>
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

// -- BOSS core dispatch threading contract ---------------------------------------------------
// Caller contract:  one chibi context per concurrent caller (a single context is not
//   thread-safe -- see docs/threading-audit.md section 3). Multiple contexts MAY call
//   BOSSEvaluate concurrently; core serves them concurrently.
// Engine contract:  an engine's `evaluate` (and `reset`) MUST be reentrant -- core may call the
//   same engine from multiple threads at once and never serializes engine calls.
// Reconfiguration:  SetDefaultEnginePipeline / ResetEngines mutate shared engine state and are
//   only valid while NO evaluation is in flight (quiesce first). They keep core's own state
//   consistent under a lock, but ResetEngines resets+unloads engines, which is unsafe under a
//   concurrent in-flight call regardless of locking.
// Implementation invariant: core takes `engineStateMutex` only to read/snapshot the pipeline
//   and resolve engine function pointers, and NEVER holds it across an engine call (that would
//   serialize the concurrency this design exists to provide, and could deadlock on re-entry).

struct LibraryAndFunctions {
  using EntryPoint = BOSSExpression* (*)(BOSSExpression*);
  void* library;
  void (*resetFunction)(void);
  EntryPoint evaluateFunction;
};

// Reset + unload libraries. MUST run OUTSIDE engineStateMutex: reset() is an engine call.
void unloadLibraries(vector<LibraryAndFunctions> const& libs) {
  for(auto const& lib : libs) {
    if(lib.resetFunction != nullptr) {
      lib.resetFunction();
    }
    dlclose(lib.library);
  }
}

class BootstrapEngine : public Engine {

  // Guards `libraries` and `defaultEnginePipeline`. Shared for the dispatch snapshot/resolve
  // path; exclusive for first-time dlopen and for reconfiguration. Never held across an engine
  // call. Declared first so the GUARDED_BY annotations below can reference it.
  mutable boss::concurrency::SharedMutex engineStateMutex;

  static string symbolToLibraryName(Symbol const& engine) {
#ifdef _WIN32
    return engine.getName() + "Engine.dll";
#else
    return "lib" + engine.getName() + "Engine.so";
#endif
  }

  static string libraryNameForEnginePath(Expression const& enginePath) {
    return visit(
        overload(
            [](Symbol const& engine) { return symbolToLibraryName(engine); },
            [](ComplexExpression const& engine) { return symbolToLibraryName(engine.getHead()); },
            [](string const& path) { return path; },
            [](auto const& /*unused*/) -> string {
              throw std::runtime_error(
                  "engine pipeline entry must be Symbol, ComplexExpression, or string");
            }),
        enginePath);
  }

  struct LibraryCache : private unordered_map<string, LibraryAndFunctions> {
    // Look up an already-loaded library. Caller holds a shared lock on engineStateMutex.
    ::std::optional<LibraryAndFunctions> tryGet(string const& libraryPath) const {
      auto const it = find(libraryPath);
      if(it == end()) {
        return ::std::nullopt;
      }
      return it->second;
    }

    // dlopen the library if absent, then return it. Caller holds an EXCLUSIVE lock.
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
          throw runtime_error("library \"" + libraryPath + "\" could not be loaded: " +
                              dlerror());
        }
      };
      return unordered_map::at(libraryPath);
    }

    // Detach all entries so the caller can unload them OUTSIDE the lock. Map becomes empty.
    vector<LibraryAndFunctions> detachAll() {
      vector<LibraryAndFunctions> libs;
      libs.reserve(size());
      for(const auto& [name, library] : *this) {
        libs.push_back(library);
      }
      unordered_map<string, LibraryAndFunctions>::clear();
      return libs;
    }

    ~LibraryCache() { unloadLibraries(detachAll()); }

    LibraryCache() = default;
    LibraryCache(LibraryCache const&) = delete;
    LibraryCache(LibraryCache&&) = delete;
    LibraryCache& operator=(LibraryCache const&) = delete;
    LibraryCache& operator=(LibraryCache&&) = delete;
  } libraries BOSS_GUARDED_BY(engineStateMutex);

  vector<Expression> defaultEnginePipeline BOSS_GUARDED_BY(engineStateMutex);

  // Resolve (loading on first use) an engine's evaluate function. Takes engineStateMutex
  // shared for the common cache hit, upgrading to exclusive only to dlopen on a miss.
  // EXCLUDES: acquires the lock itself, so callers must not already hold it.
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

  unordered_map<Symbol, std::function<Expression(ComplexExpression&&)>> const registeredOperators {
      {Symbol("EvaluateInEngines"),
       [this](auto&& e) -> Expression {
         auto resolveEngineEntryPoint = [this](auto const& enginePath) {
           auto getEntryPoint = overload(
               [](Symbol const& engine) -> string { return symbolToLibraryName(engine); },
               [](ComplexExpression const& engine) -> string {
                 return symbolToLibraryName(engine.getHead());
               },
               [&enginePath](auto const& /*unused*/) -> string { return get<string>(enginePath); });
           // Resolve the base entry point under the engine-state lock (resolveEvaluateFunction
           // locks internally and releases before returning), then do any GetEntryPoint engine
           // call below with NO lock held.
           auto* entryPoint = resolveEvaluateFunction(visit(getEntryPoint, enginePath));
           visit(overload(
                     [&entryPoint](ComplexExpression const& engine) {
                       auto [_originalHead, statics, dynamics, spans] =
                           engine
                               .clone(
                                   boss::expressions::CloneReason::CONVERSION_TO_C_BOSS_EXPRESSION)
                               .decompose();
                       auto* input = new BOSSExpression {
                           ComplexExpression(Symbol("GetEntryPoint"), std::move(statics),
                                             std::move(dynamics), std::move(spans))};
                       auto* output = entryPoint(input);
                       freeBOSSExpression(input);
                       if(output != nullptr) {
                         visit(overload(
                                   [&entryPoint](std::int64_t value) {
                                     // NOLINTNEXTLINE(performance-no-int-to-ptr)
                                     entryPoint = reinterpret_cast<LibraryAndFunctions::EntryPoint>(
                                         static_cast<std::intptr_t>(value));
                                   },
                                   [](auto const& /*unused*/) {}),
                               output->delegate);
                         freeBOSSExpression(output);
                       }
                     },
                     [](auto const& /*unused*/) {}),
                 enginePath);
           return entryPoint;
         };

         auto symbols = vector<LibraryAndFunctions::EntryPoint>();
         auto args = get<ComplexExpression>(e.getArguments().at(0)).getArguments();
         // Resolve all engine entry points up front (each resolveEvaluateFunction call locks
         // engineStateMutex briefly). The dispatch loops below then run with NO lock held.
         for_each(args.begin(), args.end(),
                  [&symbols, &resolveEngineEntryPoint](auto&& enginePath) {
                    symbols.push_back(
                        resolveEngineEntryPoint(std::forward<decltype(enginePath)>(enginePath)));
                  });
         for_each(make_move_iterator(std::next(
                      e.getArguments().begin())), // Note: first argument is the engine path
                  make_move_iterator(prev(e.getArguments().end())), [&symbols](auto&& argument) {
                    auto* wrapper = new BOSSExpression {std::forward<decltype(argument)>(argument)};
                    for(auto sym : symbols) {
                      auto* oldWrapper = wrapper;
                      wrapper = (sym(wrapper));
                      freeBOSSExpression(oldWrapper);
                    }
                    freeBOSSExpression(wrapper);
                  });

         auto* r = new BOSSExpression {*prev(e.getArguments().end())};
         for(auto sym : symbols) {
           auto* oldWrapper = r;
           r = sym(r);
           freeBOSSExpression(oldWrapper);
         }
         auto result = std::move(r->delegate);
         freeBOSSExpression(r); // NOLINT
         return std::move(result);
       }},
      {Symbol("SetDefaultEnginePipeline"),
       [this](auto&& expression) -> Expression {
         // Build the new pipeline into a local first (no lock needed, and validation throws
         // before we touch shared state -- so a bad argument leaves the pipeline unchanged).
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
         // Reconfiguration: exclusive lock. Swap in the new pipeline and detach the loaded
         // libraries to unload them OUTSIDE the lock (reset() is an engine call). Only valid
         // while no evaluation is in flight.
         vector<LibraryAndFunctions> toUnload;
         {
           boss::concurrency::UniqueLock const lock(engineStateMutex);
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
         // Snapshot the pipeline under a shared lock, then resolve + call engines with no lock.
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
         for(auto const& enginePath : pipeline) {
           auto* evalFn = resolveEvaluateFunction(libraryNameForEnginePath(enginePath));
           auto* queryWrapper = new BOSSExpression {"GetEngineDescription"_()};
           auto* resultWrapper = evalFn(queryWrapper);
           freeBOSSExpression(queryWrapper);
           auto resultExpr = std::move(resultWrapper->delegate);
           freeBOSSExpression(resultWrapper);
           auto engineName = path(libraryNameForEnginePath(enginePath)).stem().string();
           if(engineName.compare(0, 3, "lib") == 0) {
             engineName.erase(0, 3);
           }
           visit(overload(
                     [&descriptions, &engineName](string const& description) {
                       if(!descriptions.empty()) {
                         descriptions += "\n";
                       }
                       descriptions += "## " + engineName + "\n\n";
                       descriptions += description;
                     },
                     [](auto const&) {}),
                 resultExpr);
         }
         return descriptions;
       }},
      {Symbol("ResetEngines"), [this](auto&& /*expression*/) -> Expression {
         // Reconfiguration: only valid while no evaluation is in flight. Clear core state under
         // the lock, then reset+unload the engines OUTSIDE it (reset() is an engine call).
         vector<LibraryAndFunctions> toUnload;
         {
           boss::concurrency::UniqueLock const lock(engineStateMutex);
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
    for(auto const& lib : std::initializer_list<std::string> {BOSS_DEFAULT_ENGINE_LIBS}) {
      defaultEnginePipeline.push_back(lib);
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

    // Snapshot the engine pipeline under a shared lock (only for root, non-bootstrap
    // expressions -- recursive sub-evaluations and bootstrap commands never wrap). The lock is
    // released before dispatch, so it is never held across an engine call.
    ExpressionArguments pipelineArgs;
    if(isRootExpression && !isBootstrapCommand(e)) {
      boss::concurrency::SharedLock const lock(engineStateMutex);
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
                     [this](ComplexExpression&& unevaluatedE) -> Expression {
                       if(registeredOperators.count(unevaluatedE.getHead()) == 0) {
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
