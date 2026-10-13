#pragma once

#include "Algorithm.hpp"
#include "BOSS.hpp"
#include "Engine.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
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

class BootstrapEngine : public Engine {

  struct LibraryAndFunctions {
    void *library, *evaluateFunction, *resetFunction;
  };

  struct LibraryCache : private unordered_map<string, LibraryAndFunctions> {
    LibraryAndFunctions const& at(string const& libraryPath) {
      if(count(libraryPath) == 0) {
        const auto* n = libraryPath.c_str();
        if(auto* library = dlopen(n, RTLD_NOW | RTLD_NODELETE)) { // NOLINT(hicpp-signed-bitwise)
          if(auto* evalSym = dlsym(library, "evaluate")) {
            auto* resetSym = dlsym(library, "reset");
            emplace(libraryPath, LibraryAndFunctions {library, evalSym, resetSym});
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

    ~LibraryCache() { clear(); }

    void clear() {
      for(const auto& [name, library] : *this) {
        if(library.resetFunction != nullptr) {
          reinterpret_cast<void (*)(void)>(library.resetFunction)();
        }
        dlclose(library.library);
      }
      unordered_map<string, LibraryAndFunctions>::clear();
    }

    LibraryCache() = default;
    LibraryCache(LibraryCache const&) = delete;
    LibraryCache(LibraryCache&&) = delete;
    LibraryCache& operator=(LibraryCache const&) = delete;
    LibraryCache& operator=(LibraryCache&&) = delete;
  } libraries;

  vector<string> defaultEngine;

  unordered_map<Symbol, std::function<Expression(ComplexExpression&&)>> const registeredOperators {
      {Symbol("EvaluateInEngines"),
       [this](auto&& e) -> Expression {
         auto symbols = vector<BOSSExpression* (*)(BOSSExpression*)>();
         auto args = get<ComplexExpression>(e.getArguments().at(0)).getArguments();
         for_each(args.begin(), args.end(),
                  [&libraries = this->libraries, &symbols](auto&& enginePath) {
                    symbols.push_back(reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(
                        libraries.at(get<string>(enginePath)).evaluateFunction));
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
         algorithm::visitEach(expression.getArguments(), [&defaultEngine =
                                                              this->defaultEngine](auto&& engine) {
           if constexpr(std::is_same_v<std::decay_t<decltype(engine)>, string>) {
             defaultEngine.push_back(engine);
           } else {
             std::stringstream errorMessage;
             errorMessage << "SetDefaultEnginePipeline received non-string argument: " << engine;
             throw std::runtime_error(errorMessage.str());
           }
         });
         return "okay";
       }},
      {Symbol("ResetEngines"), [this](auto&& /*expression*/) -> Expression {
         libraries.clear();
         return "okay";
       }}};
  bool isBootstrapCommand(Expression const& expression) {
    return visit(overload(
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
    // pointers and construct each std::string once, directly in the vector.
    for(auto const* library : {BOSS_DEFAULT_ENGINE_LIBS}) {
      defaultEngine.emplace_back(library);
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

    auto wrappedE = isRootExpression && !defaultEngine.empty() && !isBootstrapCommand(e)
                        ? "EvaluateInEngines"_("List"_(Span<string>(vector<string>(defaultEngine))),
                                               std::move(e))
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
