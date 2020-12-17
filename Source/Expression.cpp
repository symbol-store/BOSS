#include "Expression.hpp"
#include "Utilities.hpp"
bool operator==(boss::Expression::ReturnType const& r1, boss::Expression::ReturnType const& r2) {
  if(r1.index() == r2.index()) {
    return std::visit(boss::utilities::overload(
                          [&](boss::Expression const& r1) {
                            auto const& r2Expression = std::get<boss::Expression>(r2);
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
                          [&](auto r1) { return r1 == std::get<decltype(r1)>(r2); }),
                      r1);
  }
  return false;
}
