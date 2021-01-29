#include "Bulk.hpp"

#include "Bulk/BatchTemplates.hpp"
#include "Bulk/BuiltinFunctions.hpp"

namespace boss::engines::bulk {

using BatchTemplatesImpl = BatchTemplates<bool, int, float, std::string>;

Engine::Engine() : m_batchFactory(createBatchFactory()) {}

Engine::~Engine() { delete &m_batchFactory; }

/*static*/ BatchFactory& Engine::createBatchFactory() {
  auto* templates = new BatchTemplatesImpl();
  BuiltinFunctions functionsInitialiser(*templates);
  return *templates;
}

Expression Engine::evaluate(Expression const& e) {
  auto batchPtr = m_batchFactory.createBatch(e, true);
  auto& batch = *batchPtr;
  batch.insert(e);
  auto outputPtr = batch.evaluate();
  return m_batchFactory.revertToExpression(*outputPtr);
}

} // namespace boss::engines::bulk
