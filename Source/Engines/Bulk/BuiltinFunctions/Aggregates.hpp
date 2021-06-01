#pragma once

#include "../Batch/ValueBatch.hpp"
#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Aggregates {
  using NonSymbolicBatch = typename OperatorUtils::NonSymbolicBatch;

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<CountOperator>("Count");
    operatorRegistry.template registerOperator<SumOperator>("Sum");
    operatorRegistry.template registerOperator<MinOperator>("Min");
    operatorRegistry.template registerOperator<MaxOperator>("Max");
  }

private:
  class CountOperator : public Operator<1, NonSymbolicBatch> {
  public:
    template <typename BatchType> auto evaluate(BatchType&& batchPtr) const {
      int value = batchPtr->size();
      return Batch::WritablePtr(new ValueBatch(1, value));
    }
  };

  class SumOperator : public OperatorBuilder<1>::OperatorForTypes<int, float> {
  public:
    template <typename BatchType> auto evaluate(BatchType&& batchPtr) const {
      auto it = batchPtr->begin();
      auto sum = *it;
      ++it;
      while(it != batchPtr->end()) {
        sum += *it;
        ++it;
      }
      return Batch::WritablePtr(new ValueBatch(1, sum));
    }
  };

  class MinOperator : public OperatorBuilder<1>::OperatorForTypes<int, float> {
  public:
    template <typename BatchType> auto evaluate(BatchType&& batchPtr) const {
      auto it = batchPtr->begin();
      auto min = *it;
      ++it;
      while(it != batchPtr->end()) {
        auto value = *it;
        if(value < min) {
          min = value;
        }
        ++it;
      }
      return Batch::WritablePtr(new ValueBatch(1, min));
    }
  };

  class MaxOperator : public OperatorBuilder<1>::OperatorForTypes<int, float> {
  public:
    template <typename BatchType> auto evaluate(BatchType&& batchPtr) const {
      auto it = batchPtr->begin();
      auto max = *it;
      ++it;
      while(it != batchPtr->end()) {
        auto value = *it;
        if(value > max) {
          max = value;
        }
        ++it;
      }
      return Batch::WritablePtr(new ValueBatch(1, max));
    }
  };
};

} // namespace boss::engines::bulk
