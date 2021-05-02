#include "Bulk.hpp"

#include "Bulk/BatchTemplates.hpp"
#include "Bulk/BuiltinFunctions.hpp"

namespace boss::engines::bulk {

using BatchTemplatesImpl = BatchTemplates<bool, int, float, std::string>;

Engine::Engine() : m_batchFactory(createBatchFactory()) { DefaultSymbolPool::instance().clear(); }

Engine::~Engine() { delete &m_batchFactory; }

/*static*/ BatchFactory& Engine::createBatchFactory() {
  auto* templates = new BatchTemplatesImpl();
  BuiltinFunctions<BatchTemplatesImpl>::registerAll(*templates);
  return *templates;
}

Expression Engine::evaluate(Expression const& e) {
  Batch::WritablePtr batchPtr;
  auto const* expr = std::get_if<ComplexExpression>(&e);
  if(expr != nullptr && expr->getHead() == Symbol("List") && !expr->getArguments().empty()) {
    // special case if the root head is a list
    // can just put all arguments in a single batch
    auto argsIt = expr->getArguments().begin();
    batchPtr = Batch::WritablePtr(m_batchFactory.createBatch(*argsIt));
    auto& batch = *batchPtr;
    for(++argsIt; argsIt != expr->getArguments().end(); ++argsIt) {
      batch.insert(*argsIt);
    }
  } else {
    // default case, create just a single element batch for the root expression
    batchPtr = Batch::WritablePtr(m_batchFactory.createBatch(e));
  }
  Batch::ReadablePtr outputPtr;
  if(!batchPtr->evaluate(outputPtr)) {
    return e;
  }
  return m_batchFactory.revertToExpression(std::move(outputPtr));
}

} // namespace boss::engines::bulk
