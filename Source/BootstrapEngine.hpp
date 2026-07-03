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

// ── BOSS core dispatch threading contract ──────────────────────────────────────────────────
// Caller contract:  one chibi context per concurrent caller (a single context is not
//   thread-safe — see docs/threading-audit.md §3). Multiple contexts MAY call BOSSEvaluate
//   concurrently; core serves them concurrently.
// Engine contract:  an engine's `evaluate` (and `reset`) MUST be reentrant — core may call the
//   same engine from multiple threads at once and never serializes engine calls.
// Reconfiguration:  SetDefaultEnginePipeline / ResetEngines mutate shared engine state and are
//   only valid while NO evaluation is in flight (quiesce first). They keep core's own state
//   consistent under a lock, but ResetEngines resets+unloads engines, which is unsafe under a
//   concurrent in-flight call regardless of locking.
// Implementation invariant: core takes `engineStateMutex` only to read/snapshot the pipeline
//   and resolve engine function pointers, and NEVER holds it across an engine call (that would
//   serialize the concurrency this design exists to provide, and could deadlock on re-entry).

struct LibraryAndFunctions {
  void *library, *evaluateFunction, *resetFunction;
};

// Reset + unload libraries. MUST run OUTSIDE engineStateMutex: reset() is an engine call.
void unloadLibraries(::std::vector<LibraryAndFunctions> const& libs) {
  for(auto const& lib : libs) {
    if(lib.resetFunction != nullptr) {
      reinterpret_cast<void (*)(void)>(lib.resetFunction)();
    }
    dlclose(lib.library);
  }
}

class BootstrapEngine : public boss::Engine {

  // Guards `libraries` and `defaultEngine`. Shared for the dispatch snapshot/resolve path;
  // exclusive for first-time dlopen and for reconfiguration. Never held across an engine call.
  // Declared first so the GUARDED_BY annotations below can reference it.
  mutable boss::concurrency::SharedMutex engineStateMutex;

  struct LibraryCache : private ::std::unordered_map<::std::string, LibraryAndFunctions> {
    // Look up an already-loaded library. Caller holds a shared lock on engineStateMutex.
    ::std::optional<LibraryAndFunctions> tryGet(::std::string const& libraryPath) const {
      auto const it = find(libraryPath);
      if(it == end()) {
        return ::std::nullopt;
      }
      return it->second;
    }

    // dlopen the library if absent, then return it. Caller holds an EXCLUSIVE lock.
    LibraryAndFunctions const& loadOrGet(::std::string const& libraryPath) {
      if(count(libraryPath) == 0) {
        const auto* n = libraryPath.c_str();
        if(auto* library = dlopen(n, RTLD_NOW | RTLD_NODELETE)) { // NOLINT(hicpp-signed-bitwise)
          if(auto* evalSym = dlsym(library, "evaluate")) {
            auto* resetSym = dlsym(library, "reset");
            emplace(libraryPath, LibraryAndFunctions {library, evalSym, resetSym});
          } else {
            throw ::std::runtime_error("library \"" + libraryPath +
                                       "\" does not provide an evaluate function: " + dlerror());
          }
        } else {
          throw ::std::runtime_error("library \"" + libraryPath +
                                     "\" could not be loaded: " + dlerror());
        }
      };
      return unordered_map::at(libraryPath);
    }

    // Detach all entries so the caller can unload them OUTSIDE the lock. Map becomes empty.
    ::std::vector<LibraryAndFunctions> detachAll() {
      ::std::vector<LibraryAndFunctions> libs;
      libs.reserve(size());
      for(const auto& [name, library] : *this) {
        libs.push_back(library);
      }
      ::std::unordered_map<::std::string, LibraryAndFunctions>::clear();
      return libs;
    }

    ~LibraryCache() { unloadLibraries(detachAll()); }

    LibraryCache() = default;
    LibraryCache(LibraryCache const&) = delete;
    LibraryCache(LibraryCache&&) = delete;
    LibraryCache& operator=(LibraryCache const&) = delete;
    LibraryCache& operator=(LibraryCache&&) = delete;
  } libraries BOSS_GUARDED_BY(engineStateMutex);

  ::std::vector<::std::string> defaultEngine BOSS_GUARDED_BY(engineStateMutex);

  // Resolve (loading on first use) an engine's evaluate function. Takes engineStateMutex
  // shared for the common cache hit, upgrading to exclusive only to dlopen on a miss.
  // EXCLUDES: acquires the lock itself, so callers must not already hold it.
  void* resolveEvaluateFunction(::std::string const& libraryPath) BOSS_EXCLUDES(engineStateMutex) {
    {
      boss::concurrency::SharedLock const lock(engineStateMutex);
      if(auto const entry = libraries.tryGet(libraryPath)) {
        return entry->evaluateFunction;
      }
    }
    boss::concurrency::UniqueLock const lock(engineStateMutex);
    return libraries.loadOrGet(libraryPath).evaluateFunction;
  }

  ::std::unordered_map<boss::Symbol, ::std::function<boss::Expression(
                                         boss::ComplexExpression&&)>> const registeredOperators {
      {boss::Symbol("EvaluateInEngines"),
       [this](auto&& e) -> boss::Expression {
         auto symbols = ::std::vector<BOSSExpression* (*)(BOSSExpression*)>();
         auto args = get<ComplexExpression>(e.getArguments().at(0)).getArguments();
         // Resolve all engine function pointers up front (each call locks engineStateMutex
         // briefly). The dispatch loops below then run with NO lock held.
         ::std::for_each(args.begin(), args.end(), [this, &symbols](auto&& enginePath) {
           symbols.push_back(reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(
               resolveEvaluateFunction(get<::std::string>(enginePath))));
         });
         ::std::for_each(::std::make_move_iterator(::std::next(
                             e.getArguments().begin())), // Note: first argument is the engine path
                         ::std::make_move_iterator(::std::prev(e.getArguments().end())),
                         [&symbols](auto&& argument) {
                           auto* wrapper =
                               new BOSSExpression {::std::forward<decltype(argument)>(argument)};
                           for(auto sym : symbols) {
                             auto* oldWrapper = wrapper;
                             wrapper = (sym(wrapper));
                             freeBOSSExpression(oldWrapper);
                           }
                           freeBOSSExpression(wrapper);
                         });

         auto* r = new BOSSExpression {*::std::prev(e.getArguments().end())};
         for(auto sym : symbols) {
           auto* oldWrapper = r;
           r = sym(r);
           freeBOSSExpression(oldWrapper);
         }
         auto result = ::std::move(r->delegate);
         freeBOSSExpression(r); // NOLINT
         return ::std::move(result);
       }},
      {boss::Symbol("SetDefaultEnginePipeline"),
       [this](auto&& expression) -> boss::Expression {
         // Build the new pipeline into a local first (no lock needed, and validation throws
         // before we touch shared state — so a bad argument leaves the pipeline unchanged).
         ::std::vector<::std::string> newPipeline;
         algorithm::visitEach(expression.getArguments(), [&newPipeline](auto&& engine) {
           if constexpr(::std::is_same_v<::std::decay_t<decltype(engine)>, ::std::string>) {
             newPipeline.push_back(engine);
           } else {
             std::stringstream errorMessage;
             errorMessage << "SetDefaultEnginePipeline received non-string argument: " << engine;
             throw std::runtime_error(errorMessage.str());
           }
         });
         // Reconfiguration: exclusive lock. Only valid while no evaluation is in flight.
         boss::concurrency::UniqueLock const lock(engineStateMutex);
         defaultEngine = ::std::move(newPipeline);
         return "okay";
       }},
      {boss::Symbol("GetDefaultEnginePipeline"),
       [this](auto&& /*expression*/) -> boss::Expression {
         boss::concurrency::SharedLock const lock(engineStateMutex);
         return "List"_(Span<::std::string>(::std::vector<::std::string>(defaultEngine)));
       }},
      {boss::Symbol("GetEngineDescription"),
       [this](auto&& /*expression*/) -> boss::Expression {
         using boss::utilities::operator""_;
         // Snapshot the pipeline under a shared lock, then resolve + call engines with no lock.
         ::std::vector<::std::string> pipeline;
         {
           boss::concurrency::SharedLock const lock(engineStateMutex);
           pipeline = defaultEngine;
         }
         std::string descriptions;
         for(auto const& enginePath : pipeline) {
           auto* evalFn = reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(
               resolveEvaluateFunction(enginePath));
           auto* queryWrapper = new BOSSExpression {"GetEngineDescription"_()};
           auto* resultWrapper = evalFn(queryWrapper);
           freeBOSSExpression(queryWrapper);
           auto resultExpr = ::std::move(resultWrapper->delegate);
           freeBOSSExpression(resultWrapper);
           auto engineName = ::std::filesystem::path(enginePath).stem().string();
           if(engineName.compare(0, 3, "lib") == 0) {
             engineName.erase(0, 3);
           }
           ::std::visit(boss::utilities::overload(
                            [&descriptions, &engineName](::std::string const& description) {
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
      {boss::Symbol("ResetEngines"), [this](auto&& /*expression*/) -> boss::Expression {
         // Reconfiguration: only valid while no evaluation is in flight. Clear core state under
         // the lock, then reset+unload the engines OUTSIDE it (reset() is an engine call).
         ::std::vector<LibraryAndFunctions> toUnload;
         {
           boss::concurrency::UniqueLock const lock(engineStateMutex);
           defaultEngine.clear();
           toUnload = libraries.detachAll();
         }
         unloadLibraries(toUnload);
         return "okay";
       }}};

  bool isBootstrapCommand(boss::Expression const& expression) {
    return visit(utilities::overload(
                     [this](boss::ComplexExpression const& expression) {
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
      defaultEngine.push_back(lib);
    }
#endif
  }
  ~BootstrapEngine() = default;
  BootstrapEngine(BootstrapEngine const&) = delete;
  BootstrapEngine(BootstrapEngine&&) = delete;
  BootstrapEngine& operator=(BootstrapEngine const&) = delete;
  BootstrapEngine& operator=(BootstrapEngine&&) = delete;

  auto evaluateArguments(boss::ComplexExpression&& expr) {
    ::std::transform(::std::make_move_iterator(begin(expr.getArguments())),
                     ::std::make_move_iterator(end(expr.getArguments())),
                     begin(expr.getArguments()),
                     [this](auto&& e) { return evaluate(::std::forward<decltype(e)>(e), false); });
    return ::std::move(expr);
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  boss::Expression evaluate(boss::Expression&& e, bool isRootExpression = true) {
    using boss::utilities::operator""_;

    // Snapshot the engine pipeline under a shared lock (only for root, non-bootstrap
    // expressions — recursive sub-evaluations and bootstrap commands never wrap). The lock is
    // released before dispatch, so it is never held across an engine call.
    ::std::vector<::std::string> pipeline;
    if(isRootExpression && !isBootstrapCommand(e)) {
      boss::concurrency::SharedLock const lock(engineStateMutex);
      pipeline = defaultEngine;
    }
    auto wrappedE = !pipeline.empty()
                        ? "EvaluateInEngines"_("List"_(Span<::std::string>(::std::move(pipeline))),
                                               std::move(e))
                        : std::move(e);
    return ::std::visit(boss::utilities::overload(
                            [this](boss::ComplexExpression&& unevaluatedE) -> boss::Expression {
                              if(registeredOperators.count(unevaluatedE.getHead()) == 0) {
                                return ::std::move(unevaluatedE);
                              }
                              auto const& op = registeredOperators.at(unevaluatedE.getHead());
                              return op(evaluateArguments(::std::move(unevaluatedE)));
                            },
                            [](auto&& e) -> boss::Expression { return e; }),
                        ::std::forward<boss::Expression>(wrappedE));
  }
};
} // namespace
} // namespace engines

} // namespace boss
