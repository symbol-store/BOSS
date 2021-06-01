#pragma once

#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class ArithmeticFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<PlusOperator>("Plus");
    operatorRegistry.template registerOperator<MinusOperator>("Minus");
    operatorRegistry.template registerOperator<TimesOperator>("Times");
    operatorRegistry.template registerOperator<DivOperator>("Divide");
    operatorRegistry.template registerOperator<NegOperator>("Negation");
    operatorRegistry.template registerOperator<LerpOperator>("Lerp");
  }

private:
  class PlusOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class MinusOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class TimesOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class DivOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class NegOperator : public OperatorBuilder<1>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType> auto evaluate(LhsType&& lhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a) -> auto { return -a; }, lhsBatchPtr);
    }
  };

  class LerpOperator : public OperatorBuilder<3>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType, typename RatioType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr, RatioType&& ratioBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b, auto const& t) -> auto { return a + (b - a) * t; },
          lhsBatchPtr, rhsBatchPtr, ratioBatchPtr);
    }
  };
};

} // namespace boss::engines::bulk
