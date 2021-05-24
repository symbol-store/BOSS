#include "Bulk.hpp"

#include "Bulk/BatchPrototypes.hpp"
#include "Bulk/BuiltinFunctions.hpp"
#include "Bulk/SymbolRegistry.hpp"

namespace boss::engines::bulk {

// BatchPrototypes is where we create batches for all the supported types
// we need to pass the list of atomic types we support
// so they are handled by all the compile-time boilerplate code
using BatchPrototypesImpl = BatchPrototypes<bool, int, float, std::string>;

Engine::Engine() { DefaultSymbolRegistry::instance().clear(); }

/*static*/ BatchFactory const& Engine::createBatchFactory() {
  static BatchPrototypesImpl prototypesInstance;

  // register all built-in functions here
  BuiltinFunctions<BatchPrototypesImpl>::registerAll(prototypesInstance);

  return prototypesInstance;
}

Expression Engine::evaluate(Expression const& e) { //NOLINT
  Batch::WritablePtr batchPtr;
  auto const* expr = std::get_if<ComplexExpression>(&e);
  bool done = false;
  if(expr != nullptr && expr->getHead() == Symbol("List") && !expr->getArguments().empty()) {
    // special case if the root head is a list
    // can just put all arguments in a single batch
    auto argsIt = expr->getArguments().begin();
    batchPtr = Batch::WritablePtr(getBatchFactory().createBatch(*argsIt));
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
      batch.append(exprArg);
    }
  }

  if(!done) {
    // default case, create just a single element batch for the root expression
    batchPtr = Batch::WritablePtr(getBatchFactory().createBatch(e));
  }

  Batch::ReadablePtr outputPtr;
  if(!batchPtr->evaluate(outputPtr)) {
    return e;
  }

  // transform the batch back to an expression
  return getBatchFactory().revertToExpression(std::move(outputPtr));
}

} // namespace boss::engines::bulk
