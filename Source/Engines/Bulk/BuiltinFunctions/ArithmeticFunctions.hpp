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
  }

private:
  class PlusOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    BulkExpression evaluate(LhsType const& lhs, RhsType const& rhs) const {
      return function()(lhs, rhs);
    }

    template <typename LhsValueType, typename RhsType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            RhsType const& rhs) const {
      auto rhsSingleElementArray = std::make_shared<ValueArray<RhsType> const>(lhs->length(), rhs);
      return OperatorUtils::evaluateForEachTuple(function(), lhs, rhsSingleElementArray);
    }

    template <typename LhsType, typename RhsValueType>
    BulkExpression evaluate(LhsType const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      auto lhsSingleElementArray = std::make_shared<ValueArray<LhsType> const>(rhs->length(), lhs);
      return OperatorUtils::evaluateForEachTuple(function(), lhsSingleElementArray, rhs);
    }

    template <typename LhsValueType, typename RhsValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      return OperatorUtils::evaluateForEachTuple(function(), lhs, rhs);
    }

  private:
    static auto function() {
      return [](auto const& lhs, auto const& rhs) { return lhs + rhs; };
    }
  };

  class MinusOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    BulkExpression evaluate(LhsType const& lhs, RhsType const& rhs) const {
      return function()(lhs, rhs);
    }

    template <typename LhsValueType, typename RhsType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            RhsType const& rhs) const {
      auto rhsSingleElementArray = std::make_shared<ValueArray<RhsType> const>(lhs->length(), rhs);
      return OperatorUtils::evaluateForEachTuple(function(), lhs, rhsSingleElementArray);
    }

    template <typename LhsType, typename RhsValueType>
    BulkExpression evaluate(LhsType const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      auto lhsSingleElementArray = std::make_shared<ValueArray<LhsType> const>(rhs->length(), lhs);
      return OperatorUtils::evaluateForEachTuple(function(), lhsSingleElementArray, rhs);
    }

    template <typename LhsValueType, typename RhsValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      return OperatorUtils::evaluateForEachTuple(function(), lhs, rhs);
    }

  private:
    static auto function() {
      return [](auto const& lhs, auto const& rhs) { return lhs - rhs; };
    }
  };

  class TimesOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    BulkExpression evaluate(LhsType const& lhs, RhsType const& rhs) const {
      return function()(lhs, rhs);
    }

    template <typename LhsValueType, typename RhsType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            RhsType const& rhs) const {
      auto rhsSingleElementArray = std::make_shared<ValueArray<RhsType> const>(lhs->length(), rhs);
      return OperatorUtils::evaluateForEachTuple(function(), lhs, rhsSingleElementArray);
    }

    template <typename LhsType, typename RhsValueType>
    BulkExpression evaluate(LhsType const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      auto lhsSingleElementArray = std::make_shared<ValueArray<LhsType> const>(rhs->length(), lhs);
      return OperatorUtils::evaluateForEachTuple(function(), lhsSingleElementArray, rhs);
    }

    template <typename LhsValueType, typename RhsValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      return OperatorUtils::evaluateForEachTuple(function(), lhs, rhs);
    }

  private:
    static auto function() {
      return [](auto const& lhs, auto const& rhs) { return lhs * rhs; };
    }
  };

  class DivOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType, typename RhsType>
    BulkExpression evaluate(LhsType const& lhs, RhsType const& rhs) const {
      return function()(lhs, rhs);
    }

    template <typename LhsValueType, typename RhsType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            RhsType const& rhs) const {
      auto rhsSingleElementArray = std::make_shared<ValueArray<RhsType> const>(lhs->length(), rhs);
      return OperatorUtils::evaluateForEachTuple(function(), lhs, rhsSingleElementArray);
    }

    template <typename LhsType, typename RhsValueType>
    BulkExpression evaluate(LhsType const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      auto lhsSingleElementArray = std::make_shared<ValueArray<LhsType> const>(rhs->length(), lhs);
      return OperatorUtils::evaluateForEachTuple(function(), lhsSingleElementArray, rhs);
    }

    template <typename LhsValueType, typename RhsValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      return OperatorUtils::evaluateForEachTuple(function(), lhs, rhs);
    }

  private:
    static auto function() {
      return [](auto const& lhs, auto const& rhs) { return lhs / rhs; };
    }
  };

  class NegOperator : public OperatorBuilder<1>::OperatorForTypes<int, float> {
  public:
    template <typename LhsType> BulkExpression evaluate(LhsType const& lhs) const {
      return function()(lhs);
    }

    template <typename LhsValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs) const {
      return OperatorUtils::evaluateForEachTuple(function(), lhs);
    }

  private:
    static auto function() {
      return [](auto const& lhs) { return -lhs; };
    }
  };
};

} // namespace boss::engines::bulk
