#pragma once

#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class LogicFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<AndOperator>("And");
    operatorRegistry.template registerOperator<OrOperator>("Or");
    operatorRegistry.template registerOperator<NotOperator>("Not");
  }

private:
  class AndOperator : public OperatorBuilder<2>::OperatorForTypes<bool> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> bool { return a && b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class OrOperator : public OperatorBuilder<2>::OperatorForTypes<bool> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> bool { return a || b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class NotOperator : public OperatorBuilder<1>::OperatorForTypes<bool> {
  public:
    template <typename LhsType> auto evaluate(LhsType&& lhsBatchPtr) const {
      return OperatorUtils::evaluateElements([](auto const& a) -> bool { return !a; }, lhsBatchPtr);
    }
  };
};

} // namespace boss::engines::bulk
