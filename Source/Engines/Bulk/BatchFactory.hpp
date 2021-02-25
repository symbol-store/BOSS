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

  virtual Batch::WritablePtr createBatch(Expression const& expression,
                                         bool decomposedDispatch = false) const = 0;
  virtual Batch::ReadablePtr extractFromBatch(Batch const& srcBatch, size_t index) const = 0;
  virtual Batch::ReadablePtr recomposeBatch(Batch const& srcbatch, size_t index,
                                            bool decomposedDispatch) const = 0;
  virtual Batch::ReadablePtr reduceBatch(Batch::ReadablePtr batchPtr, size_t index) const = 0;
  virtual void reduceCompoundBatch(Batch& destBatch, Batch const& srcBatch, size_t index) const = 0;
  virtual Batch::ReadablePtr convertToNonRLE(Batch::ReadablePtr batchPtr) const = 0;
  virtual Batch::ReadablePtr convertToDecomposed(Batch::ReadablePtr batchPtr) const = 0;
  virtual Expression revertToExpression(Batch::ReadablePtr&& batchPtr) const = 0;
  virtual Expression toKey(Batch const& batch) const = 0;
};

} // namespace boss::engines::bulk
