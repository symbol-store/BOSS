#include "BOSS.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include <dlfcn.h>
#include <unordered_map>

namespace boss {
class BootstrapEngine : public boss::Engine {
  std::unordered_map<std::string, void*> libraries;

public:
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  boss::Expression evaluate(boss::Expression const& e) {
    return std::visit(
        boss::utilities::overload(
            [this](boss::ComplexExpression const& e) -> boss::Expression {
              static auto const a = [this] {
                auto result =
                    std::unordered_map<boss::Symbol, std::function<boss::Expression(
                                                         boss::ComplexExpression const&)>>();
                result[boss::Symbol("EvaluateInEngine")] = [this](auto const& e) {
                  auto const& libraryPath = std::get<std::string>(e.getArguments().at(0));
                  if(libraries.count(libraryPath) == 0) {
                    if(auto library = dlopen(libraryPath.c_str(), RTLD_LAZY)) {
                      if(auto* sym = dlsym(library, "evaluate")) {
                        libraries.emplace(libraryPath, sym);
                      } else {

                        throw std::runtime_error(
                            "library \"" + libraryPath +
                            "\" does not provide an evaluate function: " + dlerror());
                      }
                    } else {
                      throw std::runtime_error("library \"" + libraryPath +
                                               "\" could not be loaded: " + dlerror());
                    }
                  }
                  // for (auto& it : {})
                  auto process = [sym = libraries.at(libraryPath)](auto const& e) {
                    auto wrapper = BOSSExpression{.delegate = e};
                    auto* r = reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(sym)(&wrapper);
                    auto result = r->delegate;
                    free(r); // NOLINT
                    return result;
                  };
                  for(auto it = next(e.getArguments().begin()); it != prev(e.getArguments().end());
                      ++it) {
                    std::visit(process, *it);
                  }
                  return std::visit(process, e.getArguments().back());
                };
                result[boss::Symbol("Plus")] = [](auto const& e) {
                  return std::visit(boss::utilities::overload(
                                        [e](int a) -> boss::Expression {
                                          for(auto it = next(e.getArguments().begin());
                                              it != e.getArguments().end(); ++it) {
                                            a += std::get<decltype(a)>(*it);
                                          }
                                          return a;
                                        },
                                        [](auto const& e) -> boss::Expression { return e; }),
                                    e.getArguments().at(0));
                };
                return std::move(result);
              }();
              return a.at(e.getHead())(e);
            },
            [](auto& e) -> boss::Expression { return e; }),
        e);
  }
};

} // namespace boss
