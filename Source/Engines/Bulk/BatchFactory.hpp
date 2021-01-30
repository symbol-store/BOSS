#pragma once

#include "Batch/Batch.hpp"

#include "../../Expression.hpp"

namespace boss::engines::bulk {

class BatchFactory {
public:
  virtual ~BatchFactory() = default;
  BatchFactory() = default;
  BatchFactory(BatchFactory const& other) = delete;
  BatchFactory(BatchFactory&& other) = delete;
  BatchFactory& operator=(BatchFactory const& other) = delete;
  BatchFactory& operator=(BatchFactory&& other) = delete;

  virtual BatchPtr createBatch(Expression const& expression,
                               bool allowDecomposedDispatch = false) const = 0;
  virtual BatchPtr extractFromBatch(Batch const& batch, size_t index) const = 0;
  virtual BatchPtr recomposeBatch(Batch const& batch, size_t index) const = 0;
  virtual BatchPtr reduceBatch(Batch const& batch, size_t index) const = 0;
  virtual void reduceCompoundBatch(Batch& destBatch, Batch const& srcBatch, size_t index) const = 0;
  virtual BatchPtr convertToNonRLE(Batch& batch) const = 0;
  virtual Expression revertToExpression(Batch const& batch) const = 0;
};

} // namespace boss::engines::bulk
