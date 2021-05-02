#pragma once

#include "Batch/Batch.hpp"

#include "../../Expression.hpp"

#include <arrow/type_fwd.h>

namespace boss::engines::bulk {

class BatchFactory {
public:
  virtual ~BatchFactory() = default;
  BatchFactory() = default;
  BatchFactory(BatchFactory const& other) = delete;
  BatchFactory(BatchFactory&& other) = delete;
  BatchFactory& operator=(BatchFactory const& other) = delete;
  BatchFactory& operator=(BatchFactory&& other) = delete;

  virtual Batch* createBatch(Expression const& expression) const = 0;
  virtual Batch* createBatch(arrow::ArrayVector&& arrays,
                             std::shared_ptr<arrow::ArrayBuilder> arrayBuilder) const = 0;

  virtual Expression revertToExpression(Batch::ReadablePtr&& batchPtr) const = 0;
};

} // namespace boss::engines::bulk
