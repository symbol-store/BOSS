#pragma once

#include "Algorithm.hpp"
#include "BOSS.hpp"
#include "Engine.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include "Utilities.hpp"

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <filesystem>
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

class BootstrapEngine : public boss::Engine {

  struct LibraryAndFunctions {
    void *library, *evaluateFunction, *resetFunction;
  };

  struct LibraryCache : private ::std::unordered_map<::std::string, LibraryAndFunctions> {
    LibraryAndFunctions const& at(::std::string const& libraryPath) {
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

    ~LibraryCache() { clear(); }

    void clear() {
      for(const auto& [name, library] : *this) {
        if(library.resetFunction != nullptr) {
          reinterpret_cast<void (*)(void)>(library.resetFunction)();
        }
        dlclose(library.library);
      }
      ::std::unordered_map<::std::string, LibraryAndFunctions>::clear();
    }

    LibraryCache() = default;
    LibraryCache(LibraryCache const&) = delete;
    LibraryCache(LibraryCache&&) = delete;
    LibraryCache& operator=(LibraryCache const&) = delete;
    LibraryCache& operator=(LibraryCache&&) = delete;
  } libraries;

  ::std::vector<::std::string> defaultEngine;

  using EngineEntryPoint = BOSSExpression* (*)(BOSSExpression*);

  // Derives the shared library file name for an engine given as a symbol or a complex expression
  // (named by its head), following the "lib" + <name> + "Engine.so" rule.
  static ::std::string engineLibraryPath(::std::string const& engineName) {
    return "lib" + engineName + "Engine.so";
  }

  // Resolves a single engine specification to the C entry point used to dispatch expressions:
  //  - A symbol: the library "lib<Name>Engine.so" is loaded and its "evaluate" function is used as
  //    the entry point.
  //  - A complex expression: the library is loaded using the same naming rule (from the head), then
  //    the library's "evaluate" function is immediately called with the given expression. If that
  //    call returns a 64-bit integer, the integer is reinterpreted as a function pointer and used as
  //    the entry point (instead of "evaluate"); otherwise "evaluate" remains the entry point.
  //  - Anything else is treated as a direct library path (legacy behaviour, and yields the
  //    "expected string" error for unsupported argument types).
  template <typename EnginePath> EngineEntryPoint resolveEngineEntryPoint(EnginePath&& enginePath) {
    if(boss::holds_alternative<boss::Symbol>(enginePath)) {
      auto const libraryPath = engineLibraryPath(boss::get<boss::Symbol>(enginePath).getName());
      return reinterpret_cast<EngineEntryPoint>(libraries.at(libraryPath).evaluateFunction);
    }
    if(boss::holds_alternative<boss::ComplexExpression>(enginePath)) {
      auto const& expression = boss::get<boss::ComplexExpression>(enginePath);
      auto const libraryPath = engineLibraryPath(expression.getHead().getName());
      auto* evaluateFunction =
          reinterpret_cast<EngineEntryPoint>(libraries.at(libraryPath).evaluateFunction);
      auto* input =
          new BOSSExpression {expression.clone(
              boss::expressions::CloneReason::CONVERSION_TO_C_BOSS_EXPRESSION)};
      auto* output = evaluateFunction(input);
      freeBOSSExpression(input);
      auto entryPoint = evaluateFunction;
      ::std::visit(boss::utilities::overload(
                       [&entryPoint](::std::int64_t value) {
                         entryPoint = reinterpret_cast<EngineEntryPoint>(
                             static_cast<::std::intptr_t>(value));
                       },
                       [](auto const& /*unused*/) {}),
                   output->delegate);
      freeBOSSExpression(output);
      return entryPoint;
    }
    return reinterpret_cast<EngineEntryPoint>(
        libraries.at(boss::get<::std::string>(enginePath)).evaluateFunction);
  }

  ::std::unordered_map<boss::Symbol, ::std::function<boss::Expression(
                                         boss::ComplexExpression&&)>> const registeredOperators {
      {boss::Symbol("EvaluateInEngines"),
       [this](auto&& e) -> boss::Expression {
         auto symbols = ::std::vector<EngineEntryPoint>();
         auto args = get<ComplexExpression>(e.getArguments().at(0)).getArguments();
         ::std::for_each(args.begin(), args.end(), [this, &symbols](auto&& enginePath) {
           symbols.push_back(
               resolveEngineEntryPoint(::std::forward<decltype(enginePath)>(enginePath)));
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
         algorithm::visitEach(expression.getArguments(), [&defaultEngine =
                                                              this->defaultEngine](auto&& engine) {
           if constexpr(::std::is_same_v<::std::decay_t<decltype(engine)>, ::std::string>) {
             defaultEngine.push_back(engine);
           } else {
             std::stringstream errorMessage;
             errorMessage << "SetDefaultEnginePipeline received non-string argument: " << engine;
             throw std::runtime_error(errorMessage.str());
           }
         });
         return "okay";
       }},
      {boss::Symbol("ResetEngines"), [this](auto&& /*expression*/) -> boss::Expression {
         libraries.clear();
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
  BootstrapEngine() = default;
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

    auto wrappedE =
        isRootExpression && !defaultEngine.empty() && !isBootstrapCommand(e)
            ? "EvaluateInEngines"_(
                  "List"_(Span<::std::string>(::std::vector<::std::string>(defaultEngine))),
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
