#pragma once

#include "../OperatorUtils.hpp"

namespace boss::engines::bulk {

template <typename BatchPrototypes> class Aggregates {
  using Utils = OperatorUtils<BatchPrototypes>;
  using NonSymbolicBatch = typename BatchPrototypes::NonSymbolicBatch;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    prototypes.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Count", [](auto&& batchPtr) {
          int value = batchPtr->size();
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(value));
        });

    prototypes.template allowedTypes<int, float>().template registerFunction<1>(
        "Sum", [](auto&& batchPtr) {
          auto it = batchPtr->begin();
          auto sum = *it;
          ++it;
          while(it != batchPtr->end()) {
            sum += *it;
            ++it;
          }
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(sum));
        });

    prototypes.template allowedTypes<int, float>().template registerFunction<1>(
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
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(min));
        });

    prototypes.template allowedTypes<int, float>().template registerFunction<1>(
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
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(max));
        });
  }
};

} // namespace boss::engines::bulk
