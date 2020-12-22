#pragma once

#include "Batch/Batch.hpp"

#include "../../Expression.hpp"

namespace boss::engines::bulk {

class BatchFactory {
public:
  virtual BatchPtr createBatch(ComplexExpression const& expression) const = 0;
};

} // namespace boss::engines::bulk
