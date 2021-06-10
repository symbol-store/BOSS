#pragma once

#include "../Executor.hpp"
#include "../ExpressionVisitDispatcher.hpp"
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

  class StringContainsQOperator : public OperatorBuilder<2>::OperatorForTypes<std::string> {
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
      return [](auto const& lhs, auto const& rhs) { return lhs.find(rhs) != std::string::npos; };
    }
  };
};

} // namespace boss::engines::bulk
