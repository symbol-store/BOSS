#include "BOSS.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include <algorithm>
#include <dlfcn.h>
#include <unordered_map>
#include <unordered_set>

namespace boss {
class BootstrapEngine : public boss::Engine {
  struct LibraryAndEvaluateFunction {
    void *library, *evaluateFunction;
  };
  std::unordered_map<std::string, LibraryAndEvaluateFunction> libraries;

public:
  ~BootstrapEngine() {
    for(const auto& [name, library] : libraries) {
      dlclose(library.library);
    }
  }
  BootstrapEngine() = default;
  BootstrapEngine(BootstrapEngine const&) = delete;
  BootstrapEngine(BootstrapEngine&&) = default;
  BootstrapEngine& operator=(BootstrapEngine const&) = delete;
  BootstrapEngine& operator=(BootstrapEngine&&) = default;

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  boss::Expression evaluate(boss::Expression const& e) {
    return std::visit(
        boss::utilities::overload(
            [this](boss::ComplexExpression const& eIn) -> boss::Expression {
              auto arguments = eIn.getArguments();
              std::transform(arguments.begin(), arguments.end(), arguments.begin(),
                             [&](auto const& e) { return evaluate(e); });
              auto e = boss::ComplexExpression(eIn.getHead(), arguments);
              static auto const a = [this] {
                auto result =
                    std::unordered_map<boss::Symbol, std::function<boss::Expression(
                                                         boss::ComplexExpression const&)>>();
                result[boss::Symbol("EvaluateInEngine")] = [this](auto const& e) {
                  auto const& libraryPath = std::get<std::string>(e.getArguments().at(0));
                  if(libraries.count(libraryPath) == 0) {
                    auto* n = libraryPath.c_str();
                    if(auto library =
                           dlopen(n, RTLD_NOW | RTLD_NODELETE)) { // NOLINT(hicpp-signed-bitwise)
                      if(auto* sym = dlsym(library, "evaluate")) {
                        libraries.emplace(libraryPath, LibraryAndEvaluateFunction{library, sym});
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
                  auto process = [sym = libraries.at(libraryPath).evaluateFunction](auto const& e) {
                    auto wrapper = BOSSExpression{.delegate = e};
                    auto* r = reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(sym)(&wrapper);
                    auto result = r->delegate;
                    freeBOSSExpression(r); // NOLINT
                    return result;
                  };
                  for(auto it = next(e.getArguments().begin()); it != prev(e.getArguments().end());
                      ++it) {
                    std::visit(process, *it);
                  }
                  return std::visit(process, e.getArguments().back());
                };
                return std::move(result);
              }();
              return (a.count(e.getHead()) == 0) ? e : a.at(e.getHead())(e);
            },
            [](auto& e) -> boss::Expression { return e; }),
        e);
  }
};

} // namespace boss
