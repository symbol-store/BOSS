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

class BootstrapEngine : public boss::Engine {

  struct LibraryAndFunctions {
    using EntryPoint = BOSSExpression* (*)(BOSSExpression*);
    void* library;
    void (*resetFunction)(void);
    EntryPoint evaluateFunction;
  };

  struct LibraryCache : private ::std::unordered_map<::std::string, LibraryAndFunctions> {
    LibraryAndFunctions const& at(::std::string const& libraryPath) {
      if(count(libraryPath) == 0) {
        const auto* n = libraryPath.c_str();
        if(auto* library = dlopen(n, RTLD_NOW | RTLD_NODELETE)) { // NOLINT(hicpp-signed-bitwise)
          if(auto* evalSym = dlsym(library, "evaluate")) {
            emplace(libraryPath, LibraryAndFunctions {
                                     library, reinterpret_cast<void (*)(void)>(dlsym(library, "reset")),
                                     reinterpret_cast<LibraryAndFunctions::EntryPoint>(evalSym)});
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

  ::std::unordered_map<boss::Symbol, ::std::function<boss::Expression(
                                         boss::ComplexExpression&&)>> const registeredOperators {
      {boss::Symbol("EvaluateInEngines"),
       [this](auto&& e) -> boss::Expression {
         auto resolveEngineEntryPoint = [this](auto const& enginePath) {
           static auto symbolToLibraryName = [](boss::Symbol const& engine) -> ::std::string {
#ifdef _WIN32
             return engine.getName() + "Engine.dll";
#else
             return "lib" + engine.getName() + "Engine.so";
#endif
           };
           auto getEntryPoint = boss::utilities::overload(
               [](boss::Symbol const& engine) -> ::std::string {
                 return symbolToLibraryName(engine);
               },
               [](boss::ComplexExpression const& engine) -> ::std::string {
                 return symbolToLibraryName(engine.getHead());
               },
               [&enginePath](auto const& /*unused*/) -> ::std::string {
                 return boss::get<::std::string>(enginePath);
               });
           auto* entryPoint = reinterpret_cast<LibraryAndFunctions::EntryPoint>(
               libraries.at(::std::visit(getEntryPoint, enginePath.getArgument()))
                   .evaluateFunction);
           ::std::visit(boss::utilities::overload(
                            [&entryPoint](boss::ComplexExpression const& engine) {
                              auto* input = new BOSSExpression {engine.clone(
                                  boss::expressions::CloneReason::CONVERSION_TO_C_BOSS_EXPRESSION)};
                              auto* output = entryPoint(input);
                              freeBOSSExpression(input);
                              if(output != nullptr) {
                                ::std::visit(
                                    boss::utilities::overload(
                                        [&entryPoint](::std::int64_t value) {
                                          // NOLINTNEXTLINE(performance-no-int-to-ptr)
                                          entryPoint =
                                              reinterpret_cast<LibraryAndFunctions::EntryPoint>(
                                                  static_cast<::std::intptr_t>(value));
                                        },
                                        [](auto const& /*unused*/) {}),
                                    output->delegate);
                                freeBOSSExpression(output);
                              }
                            },
                            [](auto const& /*unused*/) {}),
                        enginePath.getArgument());
           return entryPoint;
         };

         auto symbols = ::std::vector<LibraryAndFunctions::EntryPoint>();
         auto args = get<ComplexExpression>(e.getArguments().at(0)).getArguments();
         ::std::for_each(
             args.begin(), args.end(), [&symbols, &resolveEngineEntryPoint](auto&& enginePath) {
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
         defaultEngine.clear();
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
      {boss::Symbol("GetDefaultEnginePipeline"),
       [this](auto&& /*expression*/) -> boss::Expression {
         return "List"_(Span<::std::string>(::std::vector<::std::string>(defaultEngine)));
       }},
      {boss::Symbol("GetEngineDescription"),
       [this](auto&& /*expression*/) -> boss::Expression {
         using boss::utilities::operator""_;
         std::string descriptions;
         for(auto const& enginePath : defaultEngine) {
           auto* evalFn = reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(
               libraries.at(enginePath).evaluateFunction);
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
         defaultEngine.clear();
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
