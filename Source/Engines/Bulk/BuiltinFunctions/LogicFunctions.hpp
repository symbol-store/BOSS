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
    BulkExpression evaluate(LhsType const& lhs, RhsType const& rhs) const {
      return function()(lhs, rhs);
    }

    template <typename LhsValueType, typename RhsType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            RhsType const& rhs) const {
      auto rhsSingleElementArray = std::make_shared<ValueArray<RhsType> const>(1, rhs);
      return OperatorUtils::evaluateElements(function(), lhs, rhsSingleElementArray);
    }

    template <typename LhsType, typename RhsValueType>
    BulkExpression evaluate(LhsType const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      auto lhsSingleElementArray = std::make_shared<ValueArray<LhsType> const>(1, lhs);
      return OperatorUtils::evaluateElements(function(), lhsSingleElementArray, rhs);
    }

    template <typename LhsValueType, typename RhsValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      return OperatorUtils::evaluateElements(function(), lhs, rhs);
    }

  private:
    static auto function() {
      return [](auto const& lhs, auto const& rhs) { return lhs && rhs; };
    }
  };
  
  class OrOperator : public OperatorBuilder<2>::OperatorForTypes<bool> {
  public:
    template <typename LhsType, typename RhsType>
    BulkExpression evaluate(LhsType const& lhs, RhsType const& rhs) const {
      return function()(lhs, rhs);
    }

    template <typename LhsValueType, typename RhsType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            RhsType const& rhs) const {
      auto rhsSingleElementArray = std::make_shared<ValueArray<RhsType> const>(1, rhs);
      return OperatorUtils::evaluateElements(function(), lhs, rhsSingleElementArray);
    }

    template <typename LhsType, typename RhsValueType>
    BulkExpression evaluate(LhsType const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      auto lhsSingleElementArray = std::make_shared<ValueArray<LhsType> const>(1, lhs);
      return OperatorUtils::evaluateElements(function(), lhsSingleElementArray, rhs);
    }

    template <typename LhsValueType, typename RhsValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs,
                            std::shared_ptr<ValueArray<RhsValueType>> const& rhs) const {
      return OperatorUtils::evaluateElements(function(), lhs, rhs);
    }

  private:
    static auto function() {
      return [](auto const& lhs, auto const& rhs) { return lhs || rhs; };
    }
  };
  
  class NotOperator : public OperatorBuilder<1>::OperatorForTypes<bool> {
  public:
    template <typename LhsType> BulkExpression evaluate(LhsType const& lhs) const {
      return function()(lhs);
    }

    template <typename LhsValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<LhsValueType>> const& lhs) const {
      return OperatorUtils::evaluateElements(function(), lhs);
    }

  private:
    static auto function() {
      return [](auto const& lhs) { return !lhs; };
    }
  };
};

} // namespace boss::engines::bulk
