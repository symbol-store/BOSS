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
      return [](auto const& lhs, auto const& rhs) {
        if constexpr(std::is_convertible_v<std::decay_t<decltype(lhs)>,
                                           std::decay_t<decltype(rhs)>>) {
          return lhs == rhs;
        } else {
          return false;
        }
      };
    }
  };
  
  class NotEqualOperator : public OperatorBuilder<2>::OperatorForTypes<bool, int, float, std::string> {
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
      return [](auto const& lhs, auto const& rhs) {
        if constexpr(std::is_convertible_v<std::decay_t<decltype(lhs)>,
                                           std::decay_t<decltype(rhs)>>) {
          return lhs != rhs;
        } else {
          return false;
        }
      };
    }
  };
  
  class LessOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
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
      return [](auto const& lhs, auto const& rhs) {
        if constexpr(std::is_convertible_v<std::decay_t<decltype(lhs)>,
                                           std::decay_t<decltype(rhs)>>) {
          return lhs < rhs;
        } else {
          return false;
        }
      };
    }
  };
  
  class LessEqualOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
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
      return [](auto const& lhs, auto const& rhs) {
        if constexpr(std::is_convertible_v<std::decay_t<decltype(lhs)>,
                                           std::decay_t<decltype(rhs)>>) {
          return lhs <= rhs;
        } else {
          return false;
        }
      };
    }
  };
  
  class GreaterOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
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
      return [](auto const& lhs, auto const& rhs) {
        if constexpr(std::is_convertible_v<std::decay_t<decltype(lhs)>,
                                           std::decay_t<decltype(rhs)>>) {
          return lhs > rhs;
        } else {
          return false;
        }
      };
    }
  };

  class GreaterEqualOperator : public OperatorBuilder<2>::OperatorForTypes<int, float> {
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
      return [](auto const& lhs, auto const& rhs) {
        if constexpr(std::is_convertible_v<std::decay_t<decltype(lhs)>,
                                           std::decay_t<decltype(rhs)>>) {
          return lhs >= rhs;
        } else {
          return false;
        }
      };
    }
  };
};

} // namespace boss::engines::bulk
