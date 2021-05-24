#pragma once

#include "Batch/Batch.hpp"

#include "../../Expression.hpp"

#include <arrow/type_fwd.h>

namespace boss::engines::bulk {

/** This is the interface class for the batch creation.
 * It allows to keep it separate from the Batch types and avoid issues with cycling includes.
 * The actual implementation is in BatchPrototypes. */
class BatchFactory {
public:
  virtual ~BatchFactory() = default;
  BatchFactory() = default;
  BatchFactory(BatchFactory const& other) = delete;
  BatchFactory(BatchFactory&& other) = delete;
  BatchFactory& operator=(BatchFactory const& other) = delete;
  BatchFactory& operator=(BatchFactory&& other) = delete;

  /// create a batch for a specific expression type and insert the expression
  virtual Batch* createBatch(Expression const& expression) const = 0;

  /// consume a vector of arrays and a builder to create a batch from them
  virtual Batch* createBatch(arrow::ArrayVector&& arrays,
                             std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder) const = 0;

  /// convert the batch back to an expression
  virtual Expression revertToExpression(Batch::ReadablePtr&& batchPtr) const = 0;
};

} // namespace boss::engines::bulk
