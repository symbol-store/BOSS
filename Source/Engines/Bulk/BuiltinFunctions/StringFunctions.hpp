#pragma once

#include "../Operator.hpp"

#include <string>

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class StringFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<StringJoinOperator>("StringJoin");
    operatorRegistry.template registerOperator<StringContainsQOperator>("StringContainsQ");
  }

private:
  class StringJoinOperator : public OperatorBuilder<2>::OperatorForTypes<std::string> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> std::string { return a + b; }, lhsBatchPtr,
          rhsBatchPtr);
    }
  };

  class StringContainsQOperator : public OperatorBuilder<2>::OperatorForTypes<std::string> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
          lhsBatchPtr, rhsBatchPtr);
    }
  };
};

} // namespace boss::engines::bulk
