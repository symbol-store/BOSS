#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Translation/SexprToFunctions.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include <iostream>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>
#include <stdexcept>
#include <string>

namespace {
using namespace mlir;
using namespace boss::mlir::types;

FuncOp generateFunctionsRecursive(sexpr::CombineOp& c, OpBuilder& builder, Region* moduleRegion,
                                  int depth) {

  builder.setInsertionPointToStart(&moduleRegion->getBlocks().front());
  auto funcType = builder.getFunctionType(llvm::None, c.getResult().getType());
  auto func = builder.create<FuncOp, llvm::StringRef, mlir::FunctionType&>(
      builder.getUnknownLoc(), "fn" + std::to_string(depth), funcType);

  func.setVisibility(mlir::SymbolTable::Visibility::Private);

  func.addEntryBlock();

  func.getBody().takeBody(c.getRegion());

  builder.setInsertionPointAfter(c.getOperation());
  auto call = builder.create<mlir::CallOp, mlir::FuncOp&>(builder.getUnknownLoc(), func);
  c.replaceAllUsesWith(call.getOperation());

  c.erase();

  for(auto combineOp : func.getRegion().getOps<sexpr::CombineOp>()) {
    generateFunctionsRecursive(combineOp, builder, moduleRegion, depth + 1);
  }

  return func;
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

  auto* newOp = builder.clone(*rootCombine.getOperation());
  builder.create<ReturnOp, ValueRange>(builder.getUnknownLoc(), newOp->getResult(0));

  rootCombine.erase();

  auto newCombineOp = mlir::dyn_cast<sexpr::CombineOp, Operation>(
      func.getBody().getBlocks().front().getOperations().front());

  generateFunctionsRecursive(newCombineOp, builder, &getOperation().getBodyRegion(), 0);
}

}; // namespace

std::unique_ptr<mlir::Pass> createLowerToFunctionsPass(RuntimeTypes& returnType) {
  return std::make_unique<SexprToFunctionsLoweringPass>(returnType);
}
