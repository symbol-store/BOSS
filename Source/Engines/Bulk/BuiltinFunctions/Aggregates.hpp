#pragma once

#include "../Batch/ValueBatch.hpp"
#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Aggregates {
  using AnyAgregableCollectionArgument =
      AllowedArguments<std::shared_ptr<ValueArray<bool>>, std::shared_ptr<ValueArray<int>>,
                       std::shared_ptr<ValueArray<float>>,
                       std::shared_ptr<ValueArray<std::string>>>;

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<SumOperator>("Sum");
    operatorRegistry.template registerOperator<MinOperator>("Min");
    operatorRegistry.template registerOperator<MaxOperator>("Max");
  }

private:
  class SumOperator : public Operator<1, AnyAgregableCollectionArgument> {
  public:
    template <typename ArrayType>
    BulkExpression evaluate(std::shared_ptr<ArrayType> const& arrayPtr) const {
      using ValueType = typename ArrayType::ValueType;
      auto it = arrayPtr->begin();
      auto sum = (ValueType)*it;
      ++it;
      for(; it != arrayPtr->end(); ++it) {
        sum += (ValueType)*it;
      }
      return sum;
    }
  };

  class MinOperator : public Operator<1, AnyAgregableCollectionArgument> {
  public:
    template <typename ArrayType>
    BulkExpression evaluate(std::shared_ptr<ArrayType> const& arrayPtr) const {
      using ValueType = typename ArrayType::ValueType;
      auto it = arrayPtr->begin();
      auto min = (ValueType)*it;
      ++it;
      for(; it != arrayPtr->end(); ++it) {
        auto value = (ValueType)*it;
        if(value < min) {
          min = value;
        }
      }
      return min;
    }
  };

  class MaxOperator : public Operator<1, AnyAgregableCollectionArgument> {
  public:
    template <typename ArrayType>
    BulkExpression evaluate(std::shared_ptr<ArrayType> const& arrayPtr) const {
      using ValueType = typename ArrayType::ValueType;
      auto it = arrayPtr->begin();
      auto max = (ValueType)*it;
      ++it;
      for(; it != arrayPtr->end(); ++it) {
        auto value = (ValueType)*it;
        if(value > max) {
          max = value;
        }
      }
      return max;
    }
  };
};

} // namespace boss::engines::bulk
