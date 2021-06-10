#pragma once

#include "../../ExpressionUtilities.hpp"

namespace boss::engines::bulk {

/** Used to visit only a subset of types from the expression */
template <typename... ArgumentTypes> class ExpressionVisitDispatcher {
public:
  template <typename Func> static bool visit(Func&& func, BulkExpression const& expression) {
    return std::visit(utilities::overload([](auto const& /*arg*/) { return false; },
                                          [&func](ArgumentTypes const& arg) {
                                            func(arg);
                                            return true;
                                          }...),
                      (BulkExpression::SuperType const&)expression);
  }
};

} // namespace boss::engines::bulk
