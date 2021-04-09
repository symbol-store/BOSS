#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Translation/SexprToFunctions.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include <mlir/IR/BlockAndValueMapping.h>
#include <iostream>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>
#include <stdexcept>
#include <string>

namespace {
using namespace mlir;
using namespace boss::mlir::types;

Value flattenCallsRecursive(sexpr::CombineOp& c, OpBuilder& builder, FuncOp function) {
  // Recursively flatten al child combineOps
  for(auto combineOp : c.getRegion().getOps<sexpr::CombineOp>()) {
    auto val = flattenCallsRecursive(combineOp, builder, function);

    combineOp.replaceAllUsesWith(val);
    combineOp.erase();
  }

  // Clone all the operations into the function body
  for (auto& op : c.getRegion().getBlocks().front().without_terminator()) {
    auto* clone = op.clone();
    op.replaceAllUsesWith(clone->getResults());
    function.getRegion().getBlocks().front().push_back(clone);
  }

  // Extract what the combine op returned, so it can be passed to the caller
  auto terminator = *c.getRegion().getOps<sexpr::EndOp>().begin();
  auto returnVal = terminator.getOperand();

  return returnVal;
}

struct SexprToFunctionsLoweringPass
    : public PassWrapper<SexprToFunctionsLoweringPass, OperationPass<ModuleOp>> {

  SexprToFunctionsLoweringPass(RuntimeTypes& returnType)
      : PassWrapper(), returnType(returnType) {}

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<StandardOpsDialect>();
  }

  RuntimeTypes& returnType;

  void runOnOperation() final;
};

// Sets the return type enum so we know what type the expression returns as a whole
void setReturnType(RuntimeTypes& returnType, mlir::Type const& actualType) {
  returnType = boss::mlir::conversion::mlirTypeToRuntimeType(actualType, true);
}

/// This pass flattens all nested CombineOps into straight-line code
void SexprToFunctionsLoweringPass::runOnOperation() {
  // Make a builder
  OpBuilder builder(getOperation().getContext());

  auto* moduleBody = getOperation().getBody();

  // Create function to surround base op
  // Find the root combine operation
  auto rootCombine = mlir::dyn_cast<sexpr::CombineOp, Operation>(getOperation().getBody()->front());

  if(!rootCombine) {
    throw std::runtime_error("Expecting combine at root");
  }

  builder.setInsertionPointToStart(moduleBody);

  auto rootType = rootCombine.getResult().getType();

  // Mark the root type so we know what value to extract from JIT after executing
  setReturnType(returnType, rootType);

  // Create the root entry function
  auto funcType = builder.getFunctionType(llvm::None, rootCombine.getResult().getType());
  auto func = builder.create<FuncOp, llvm::StringRef, mlir::FunctionType&>(builder.getUnknownLoc(),
                                                                           "entry", funcType);
  auto* entry = func.addEntryBlock();
  builder.setInsertionPointToStart(entry);

  auto lastVal = flattenCallsRecursive(rootCombine, builder, func);

  // Create new returnOp
  builder.create<ReturnOp>(rootCombine.getLoc(), lastVal);

  // Erase the combine that we flattened
  rootCombine.erase();
}

}; // namespace

std::unique_ptr<mlir::Pass> createLowerToFunctionsPass(RuntimeTypes& returnType) {
  return std::make_unique<SexprToFunctionsLoweringPass>(returnType);
}
