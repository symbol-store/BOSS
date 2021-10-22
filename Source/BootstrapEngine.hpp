#include "BOSS.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include <dlfcn.h>
#include <unordered_map>

namespace boss {
class BootstrapEngine : public boss::Engine {
public:
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  boss::Expression evaluate(boss::Expression const& e) {
    return std::visit(
        boss::utilities::overload(
            [](boss::ComplexExpression const& e) -> boss::Expression {
              std::unordered_map<boss::Symbol,
                                 std::function<boss::Expression(boss::ComplexExpression const&)>>
                  a{{boss::Symbol("EvaluateInEngine"),
                     [](auto const& e) {
                       auto libraryPath = std::get<std::string>(e.getArguments().at(0));
                       return std::visit(
                           [libraryPath,
                            library = dlopen(libraryPath.c_str(), RTLD_LAZY)](auto const& e) {
                             auto* sym = dlsym(library, "evaluate");
                             if(sym == nullptr) {
                               throw std::runtime_error("library \"" + libraryPath +
                                                        "\" does not provide an evaluate function");
                             }
                             auto wrapper = BOSSExpression{.delegate = e};
                             auto* r = reinterpret_cast<BOSSExpression* (*)(BOSSExpression*)>(sym)(
                                 &wrapper);
                             auto result = r->delegate;
                             free(r); // NOLINT
                             return result;
                           },
                           e.getArguments().at(1));
                     }},
                    {boss::Symbol("Plus"), [](auto const& e) {
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
                     }}};
              return a.at(e.getHead())(e);
            },
            [](auto& e) -> boss::Expression { return e; }),
        e);
  }
};

} // namespace boss
