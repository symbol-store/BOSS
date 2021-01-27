#include "Bulk.hpp"

namespace boss::engines::bulk {

Expression Engine::evaluate(Expression const& e) {
  auto batchPtr = m_batchTemplates.createBatch(e, true);
  auto& batch = *batchPtr;
  batch.insert(e);
  auto outputPtr = batch.evaluate();
  return m_batchTemplates.revertToExpression(*outputPtr);
}

} // namespace boss::engines::bulk
