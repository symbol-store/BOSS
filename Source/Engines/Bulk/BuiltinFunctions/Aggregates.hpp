#pragma once

#include "../Batch/ValueBatch.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Aggregates {
  using NonSymbolicBatch = typename OperatorUtils::NonSymbolicBatch;

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Count", [](auto&& batchPtr) {
          int value = batchPtr->size();
          return Batch::WritablePtr(new ValueBatch(1, value));
        });

    operatorRegistry.template allowedTypes<int, float>().template registerFunction<1>(
        "Sum", [](auto&& batchPtr) {
          auto it = batchPtr->begin();
          auto sum = *it;
          ++it;
          while(it != batchPtr->end()) {
            sum += *it;
            ++it;
          }
          return Batch::WritablePtr(new ValueBatch(1, sum));
        });

    operatorRegistry.template allowedTypes<int, float>().template registerFunction<1>(
        "Min", [](auto&& batchPtr) {
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
        });

    operatorRegistry.template allowedTypes<int, float>().template registerFunction<1>(
        "Max", [](auto&& batchPtr) {
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
        });
  }
};

} // namespace boss::engines::bulk
