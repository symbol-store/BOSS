#pragma once

#include "BOSS.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include "Utilities.hpp"

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
constexpr static int RTLD_NOW = 0;
constexpr static int RTLD_NODELETE = 0;
static void* dlopen(LPCSTR lpLibFileName, int /*flags*/) { return LoadLibrary(lpLibFileName); }
static auto dlclose(void* hModule) {
  auto resetFunction = GetProcAddress((HMODULE)hModule, "reset");
  if(resetFunction != NULL) {
    (*reinterpret_cast<void (*)()>(resetFunction))();
  }
  return FreeLibrary((HMODULE)hModule);
}
static auto dlerror() {
  auto errorCode = GetLastError();
  auto psz = LPTSTR(nullptr);
  auto msg = FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
      NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), psz, 0, NULL);
  if(msg > 0) {
    // Assign buffer to smart pointer with custom deleter so that memory gets released
    // in case String's constructor throws an exception.
    auto deleter = [](void* p) { ::LocalFree(p); };
    std::unique_ptr<TCHAR, decltype(deleter)> ptrBuffer(psz, deleter);
    return std::string(ptrBuffer.get(), msg);
  }
  return std::to_string(errorCode);
}
static void* dlsym(void* hModule, LPCSTR lpProcName) {
  return GetProcAddress((HMODULE)hModule, lpProcName);
}
#endif // _WIN32

#include <algorithm>
#include <iterator>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <variant>

using boss::utilities::operator""_;

namespace boss {
class BootstrapEngine : public boss::Engine {
  struct LibraryAndEvaluateFunction {
    void *library, *evaluateFunction;
  };

  struct LibraryCache : private std::unordered_map<std::string, LibraryAndEvaluateFunction> {
    LibraryAndEvaluateFunction const& at(std::string const& libraryPath) {
      if(count(libraryPath) == 0) {
        const auto* n = libraryPath.c_str();
        if(auto* library = dlopen(n, RTLD_NOW | RTLD_NODELETE)) { // NOLINT(hicpp-signed-bitwise)
          if(auto* sym = dlsym(library, "evaluate")) {
            emplace(libraryPath, LibraryAndEvaluateFunction{library, sym});
          } else {
            throw std::runtime_error("library \"" + libraryPath +
                                     "\" does not provide an evaluate function: " + dlerror());
          }
        } else {
          throw std::runtime_error("library \"" + libraryPath +
                                   "\" could not be loaded: " + dlerror());
        }
      };
      return unordered_map::at(libraryPath);
    }
    ~LibraryCache() {
      for(const auto& [name, library] : *this) {
        dlclose(library.library);
      }
    }

    LibraryCache() = default;
    LibraryCache(LibraryCache const&) = delete;
    LibraryCache(LibraryCache&&) = delete;
    LibraryCache& operator=(LibraryCache const&) = delete;
    LibraryCache& operator=(LibraryCache&&) = delete;
  } libraries;

  std::optional<std::string> defaultEngine = {};

  std::unordered_map<boss::Symbol, std::function<boss::Expression(boss::ComplexExpression&&)>> const
      registeredOperators{
          {boss::Symbol("EvaluateInEngine"),
           [this](auto&& e) -> boss::Expression {
             auto sym = reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(
                 libraries.at(boss::get<std::string>(e.getArguments().at(0))).evaluateFunction);
             auto processArgumentInEngine = [&sym](auto&& e) {
               auto wrapper = BOSSExpression{std::forward<decltype(e)>(e)};
               auto* r = sym(&wrapper);
               auto result = std::move(r->delegate);
               freeBOSSExpression(r); // NOLINT
               return result;
             };
             return std::accumulate(
                 std::make_move_iterator(
                     next(e.getArguments().begin())), // Note: first argument is the engine path
                 std::make_move_iterator(e.getArguments().end()), boss::Expression(0L),
                 [&processArgumentInEngine](auto&& /* we evaluate all arguments
                                                   but only return the last result */
                                            ,
                                            auto&& argument) -> boss::Expression {
                   return std::visit(processArgumentInEngine,
                                     std::forward<decltype(argument)>(argument));
                 });
           }},
          {boss::Symbol("SetDefaultEngine"), [this](auto&& expression) -> boss::Expression {
             defaultEngine = boss::get<std::string>(std::move(expression.getArguments().at(0)));
             return "okay";
           }}};
  bool isBootstrapCommand(boss::Expression const& expression) {
    return std::visit(utilities::overload(
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
    std::transform(std::make_move_iterator(expr.getArguments().begin()),
                   std::make_move_iterator(expr.getArguments().end()), begin(expr.getArguments()),
                   [&](auto&& e) { return evaluate(e, false); });
    return std::move(expr);
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  boss::Expression evaluate(boss::Expression const& e, bool isRootExpression = true) {
    auto wrappedE = isRootExpression && defaultEngine.has_value() && !isBootstrapCommand(e)
                        ? "EvaluateInEngine"_(*defaultEngine, e.clone())
                        : e.clone();
    return std::visit(boss::utilities::overload(
                          [this](boss::ComplexExpression&& unevaluatedE) -> boss::Expression {
                            if(registeredOperators.count(unevaluatedE.getHead()) == 0) {
                              return std::move(unevaluatedE);
                            }
                            auto const& op = registeredOperators.at(unevaluatedE.getHead());
                            return op(evaluateArguments(std::move(unevaluatedE)));
                          },
                          [](auto&& e) -> boss::Expression { return e; }),
                      std::forward<boss::Expression>(wrappedE));
  }
};

} // namespace boss
