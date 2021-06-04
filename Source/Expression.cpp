#include "Expression.hpp"
#include "Utilities.hpp"
using namespace boss;
bool operator==(Expression const& r1, Expression const& r2) {
  if(r1.index() == r2.index()) {
    return std::visit(
        utilities::overload(
            [&](ComplexExpression const& r1) {
              auto const& r2Expression = std::get<ComplexExpression>(r2);
              if(r1.getHead() != r2Expression.getHead() ||
                 r1.getArguments().size() != r2Expression.getArguments().size()) {
                return false;
              }
              for(auto i = 0u; i < r1.getArguments().size(); i++) {
                if(r1.getArguments()[i] != r2Expression.getArguments()[i]) {
                  return false;
                }
              }
              return true;
            },
            [&](Symbol const& r1) { return r1.getName() == std::get<Symbol>(r2).getName(); },
            [&](auto r1) { return r1 == std::get<decltype(r1)>(r2); }),
        (Expression::SuperType const&)r1);
  }
  return false;
}
