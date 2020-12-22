#include "Expression.hpp"
#include "Utilities.hpp"
bool operator==(boss::Expression const& r1, boss::Expression const& r2) {
  if(r1.index() == r2.index()) {
    return std::visit(
        boss::utilities::overload(
            [&](boss::ComplexExpression const& r1) {
              auto const& r2Expression = std::get<boss::ComplexExpression>(r2);
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
            [&](boss::Symbol r1) { return r1.getName() == std::get<decltype(r1)>(r2).getName(); },
            [&](auto r1) { return r1 == std::get<decltype(r1)>(r2); }),
        r1);
  }
  return false;
}
