#pragma once

#include "../../../Expression.hpp"

namespace boss::engines::bulk {

template <bool DispatchArgumentTypes, bool DispatchSymbolNames> struct CompareExpression {
  bool operator()(Expression const& lhs, Expression const& rhs) const {
    return compare(lhs, rhs) < 0;
  }

private:
  int compare(Expression const& lhs, Expression const& rhs) const {
    if(lhs.index() != rhs.index()) {
      return lhs.index() < rhs.index() ? -1 : 1;
    }

    if constexpr(DispatchSymbolNames) {
      if(auto const* lhsSymbol = std::get_if<Symbol>(&lhs)) {
        auto const& rhsSymbol = std::get<Symbol>(rhs);
        return lhsSymbol->getName() < rhsSymbol.getName() ? -1 : 1;
      }
    }

    if(auto const* lhsExpr = std::get_if<ComplexExpression>(&lhs)) {
      auto const& rhsExpr = std::get<ComplexExpression>(rhs);
      if(lhsExpr->getHead().getName() != rhsExpr.getHead().getName()) {
        return lhsExpr->getHead().getName() < rhsExpr.getHead().getName() ? -1 : 1;
      }

      auto lhsArgsIt = lhsExpr->getArguments().begin();
      auto rhsArgsIt = rhsExpr.getArguments().begin();
      auto lhsArgsItEnd = lhsExpr->getArguments().end();
      auto rhsArgsItEnd = rhsExpr.getArguments().end();
      size_t lhsNumArgs = std::distance(lhsArgsIt, lhsArgsItEnd);
      size_t rhsNumArgs = std::distance(rhsArgsIt, rhsArgsItEnd);

      if(lhsNumArgs != rhsNumArgs) {
        return lhsNumArgs < rhsNumArgs ? -1 : 1;
      }

      if constexpr(DispatchArgumentTypes) {
        while(lhsArgsIt != lhsArgsItEnd /*&& rhsArgsIt != rhsArgsItEnd*/) {
          int argCompare = compare(*lhsArgsIt, *rhsArgsIt);
          if(argCompare != 0) {
            return argCompare;
          }
          ++lhsArgsIt;
          ++rhsArgsIt;
        }
      }
    }

    // "normal" values (of identical type) are all dispatched to the same array
    return 0;
  }
};

} // namespace boss::engines::bulk
