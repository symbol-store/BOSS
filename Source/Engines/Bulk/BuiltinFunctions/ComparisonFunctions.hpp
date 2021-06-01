#pragma once

#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class ComparisonFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<EqualOperator>("Equal");
    operatorRegistry.template registerOperator<NotEqualOperator>("NotEqual");
    operatorRegistry.template registerOperator<LessOperator>("Less");
    operatorRegistry.template registerOperator<LessEqualOperator>("LessEqual");
    operatorRegistry.template registerOperator<GreaterOperator>("Greater");
    operatorRegistry.template registerOperator<GreaterEqualOperator>("GreaterEqual");
  }

private:
  class EqualOperator : public OperatorBuilder<2>::OperatorForTypes<bool, int, float, std::string> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
      using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
      using ValueTypeA = typename BatchTypeA::ValueType;
      using ValueTypeB = typename BatchTypeB::ValueType;
      if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
        return OperatorUtils::evaluateElements(
            [](auto const& a, auto const& b) -> bool {
              return static_cast<ValueTypeA>(a) == static_cast<ValueTypeB>(b);
            },
            lhsBatchPtr, rhsBatchPtr);
      } else {
        return OperatorUtils::evaluateElements(
            [](auto const& /*a*/, auto const& /*b*/) -> bool { return false; }, lhsBatchPtr,
            rhsBatchPtr);
      }
    }
  };

  class NotEqualOperator
      : public OperatorBuilder<2>::OperatorForTypes<bool, int, float, std::string> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
      using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
      using ValueTypeA = typename BatchTypeA::ValueType;
      using ValueTypeB = typename BatchTypeB::ValueType;
      if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
        return OperatorUtils::evaluateElements(
            [](auto const& a, auto const& b) -> bool {
              return static_cast<ValueTypeA>(a) != static_cast<ValueTypeB>(b);
            },
            lhsBatchPtr, rhsBatchPtr);
      } else {
        return OperatorUtils::evaluateElements(
            [](auto const& /*a*/, auto const& /*b*/) -> bool { return true; }, lhsBatchPtr,
            rhsBatchPtr);
      }
    }
  };

  class LessOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> bool { return a < b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class LessEqualOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> bool { return a <= b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class GreaterOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> bool { return a > b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };

  class GreaterEqualOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    auto evaluate(LhsType&& lhsBatchPtr, RhsType&& rhsBatchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& a, auto const& b) -> bool { return a >= b; }, lhsBatchPtr, rhsBatchPtr);
    }
  };
};

} // namespace boss::engines::bulk
