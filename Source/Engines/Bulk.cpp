#include "Bulk.hpp"

#include "Bulk/BatchTemplates.hpp"
#include "Bulk/BuiltinFunctions.hpp"

namespace boss::engines::bulk {

// BatchTemplates is where we create batches for all the supported types
// we need to pass the list of atomic types we support
// so they are handled by all the compile-time boilerplate code
using BatchTemplatesImpl = BatchTemplates<bool, int, float, std::string>;

Engine::Engine() : batchFactory((*new BatchTemplatesImpl())) {
  DefaultSymbolPool::instance().clear();

  // register all built-in functions here
  auto& batchTemplates = static_cast<BatchTemplatesImpl&>(batchFactory);
  BuiltinFunctions<BatchTemplatesImpl>::registerAll(batchTemplates);
}

Engine::~Engine() { delete &batchFactory; }

Expression Engine::evaluate(Expression const& e) {
  Batch::WritablePtr batchPtr;
  auto const* expr = std::get_if<ComplexExpression>(&e);
  bool done = false;
  if(expr != nullptr && expr->getHead() == Symbol("List") && !expr->getArguments().empty()) {
    // special case if the root head is a list
    // can just put all arguments in a single batch
    auto argsIt = expr->getArguments().begin();
    batchPtr = Batch::WritablePtr(batchFactory.createBatch(*argsIt));
    auto& batch = *batchPtr;

    // still check if all arguments are compatible
    // if not need to fallback to normal method
    done = true;
    for(++argsIt; argsIt != expr->getArguments().end(); ++argsIt) {
      auto const& exprArg = *argsIt;
      if(!batch.canContain(exprArg)) {
        done = false;
        break;
      }
      batch.insert(exprArg);
    }
  }
  
  if(!done) {
    // default case, create just a single element batch for the root expression
    batchPtr = Batch::WritablePtr(batchFactory.createBatch(e));
  }

  Batch::ReadablePtr outputPtr;
  if(!batchPtr->evaluate(outputPtr)) {
    return e;
  }

  // transform the batch back to an expression
  return batchFactory.revertToExpression(std::move(outputPtr));
}

} // namespace boss::engines::bulk
