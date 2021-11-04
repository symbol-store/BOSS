#include "BOSS.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include <algorithm>
#include <dlfcn.h>
#include <iterator>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace boss {
class BootstrapEngine : public boss::Engine {
  struct LibraryAndEvaluateFunction {
    void *library, *evaluateFunction;
  };
  struct LibraryCache : private std::unordered_map<std::string, LibraryAndEvaluateFunction> {
    auto at(std::string const& libraryPath) {
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

public:
  BootstrapEngine() = default;
  ~BootstrapEngine() = default;
  BootstrapEngine(BootstrapEngine const&) = delete;
  BootstrapEngine(BootstrapEngine&&) = delete;
  BootstrapEngine& operator=(BootstrapEngine const&) = delete;
  BootstrapEngine& operator=(BootstrapEngine&&) = delete;

  auto evaluateArguments(boss::ComplexExpression const& eIn) {
    auto evaluatedArguments = decltype(eIn.getArguments()){};
    std::transform(begin(eIn.getArguments()), end(eIn.getArguments()),
                   std::back_inserter(evaluatedArguments),
                   [&](auto const& e) { return evaluate(e); });
    return boss::ComplexExpression(eIn.getHead(), evaluatedArguments);
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  boss::Expression evaluate(boss::Expression const& e) {
    return std::visit(
        boss::utilities::overload(
            [this](boss::ComplexExpression const& eIn) -> boss::Expression {
              auto e = evaluateArguments(eIn);
              static auto const registeredOperators = [this] {
                auto registeredOperators =
                    std::unordered_map<boss::Symbol, std::function<boss::Expression(
                                                         boss::ComplexExpression const&)>>();
                registeredOperators[boss::Symbol("EvaluateInEngine")] =
                    [this](auto const& e) -> boss::Expression {
                  return std::accumulate(
                      begin(e.getArguments()), end(e.getArguments()), boss::Expression(0),
                      [processArgumentInEngine =
                           [sym = libraries.at(std::get<std::string>(e.getArguments().at(0)))
                                      .evaluateFunction](auto const& e) {
                             auto wrapper = BOSSExpression{.delegate = e};
                             auto* r = reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(sym)(
                                 &wrapper);
                             auto result = r->delegate;
                             freeBOSSExpression(r); // NOLINT
                             return result;
                           }](auto const& /* we evaluate all arguments but only return the last
                                             result */
                              ,
                              auto const& argument) -> boss::Expression {
                        return std::visit(processArgumentInEngine, argument);
                      });
                };
                return std::move(registeredOperators);
              }();
              return (registeredOperators.count(e.getHead()) == 0)
                         ? e
                         : registeredOperators.at(e.getHead())(e);
            },
            [](auto& e) -> boss::Expression { return e; }),
        e);
  }
};

} // namespace boss
