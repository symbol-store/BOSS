#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Translation/SexprToFunctions.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include <iostream>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/IR/BlockAndValueMapping.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>
#include <stdexcept>
#include <string>

namespace {
using namespace mlir;
using namespace boss::mlir::types;

Value flattenCallsRecursive(sexpr::CombineOp& c, OpBuilder& builder, FuncOp function);

bool hasAttribute(std::string const& name, sexpr::SymbolOp op) {
  for (auto const& attr : op.getAttrs()) {
    if (attr.first.str() == name) {
      op.removeAttr("temp");
      return true;
    }
  }
  return false;
}

llvm::Optional<Value> replaceSymbol(Operation* op, FuncOp function) {
  auto symbol = mlir::dyn_cast_or_null<sexpr::SymbolOp>(op);
  if (!symbol) {
    return {};
  }
  if (!hasAttribute("functionArgPosition", symbol)) {
    return {};
  }
  auto symbolArgPosition = symbol.getAttrOfType<::mlir::IntegerAttr>("functionArgPosition");
  auto argPosition = symbolArgPosition.getInt();
  auto argument = function.getArgument(argPosition);
  return argument;
}

Value flattenCallsStandard(sexpr::CombineOp& c, OpBuilder& builder, FuncOp function) {
  // Recursively flatten al child combineOps
  for(auto combineOp : c.getRegion().getOps<sexpr::CombineOp>()) {
    auto val = flattenCallsRecursive(combineOp, builder, function);
    combineOp.replaceAllUsesWith(val);
  }

  // Erase all the combineOps (cannot erase during normal iterator)
  while(c.getRegion().getOps<sexpr::CombineOp>().begin() !=
        c.getRegion().getOps<sexpr::CombineOp>().end()) {
    (*c.getRegion().getOps<sexpr::CombineOp>().begin()).erase();
  }

  // Clone all the operations into the function body
  for(auto& op : c.getRegion().getBlocks().front().without_terminator()) {
    // Check whether the op is a symbol, if it is, replace with argument
    auto maybeNewValue = replaceSymbol(&op, function);
    if (maybeNewValue.hasValue()) {
      auto newValue = maybeNewValue.getValue();
      op.replaceAllUsesWith(ValueRange{newValue});
    } else {
      // Else, just copy over the op
      auto* clone = op.clone();
      op.replaceAllUsesWith(clone->getResults());
      function.getRegion().getBlocks().front().push_back(clone);
    }
  }

  // Extract what the combine op returned, so it can be passed to the caller
  auto terminator = *c.getRegion().getOps<sexpr::EndOp>().begin();
  auto returnVal = terminator.getOperand();

  return returnVal;
}

Value flattenCallsWhereSymbol(sexpr::CombineOp& c, OpBuilder& builder, FuncOp function) {
  static int generatedFuncs = 0;
  auto sOrVType = c.getType().dyn_cast_or_null<SymbolOrValueType>();
  if (!sOrVType) {
    throw std::runtime_error("Expecting symbol Where type to be wrapped in sOrVType");
  }
  auto type = sOrVType.getBaseType().dyn_cast_or_null<GenericTupleStreamUnionType>();
  if (!type) {
    throw std::runtime_error("Expecting symbol Where to return union of functions");
  }

  std::vector<::mlir::Attribute> functionNames;
  for (auto const& childType : type.getChildren()) {
    auto savedInsertionPoint = builder.saveInsertionPoint();
    builder.setInsertionPointToStart(function.getParentOfType<ModuleOp>().getBody());
    auto funcType = childType.dyn_cast_or_null<FunctionType>();
    if (!funcType) {
      throw std::runtime_error("Expecting symbol Where to return union of functions");
    }
    auto funcName = "select" + std::to_string(generatedFuncs++);
    auto childFunc = builder.create<FuncOp>(c.getLoc(), funcName, funcType);
    auto* funcBlock = childFunc.addEntryBlock();
    builder.setInsertionPointToStart(funcBlock);

    for (auto combineOp : c.getRegion().getOps<sexpr::CombineOp>()) {
      // For each input, lower the function body
      auto combineCopy = combineOp.clone();
      auto lastVal = flattenCallsRecursive(combineCopy, builder, childFunc);
      combineCopy.erase();
      // Create new returnOp
      builder.create<ReturnOp>(c.getLoc(), lastVal);
    }
    builder.restoreInsertionPoint(savedInsertionPoint);

    functionNames.emplace_back(::mlir::StringAttr::get(funcName, c.getContext()));
  }

  auto newUnionStream = builder.create<sexpr::SymbolOp>(c.getLoc(), sOrVType, "Where", ::mlir::ValueRange());
  newUnionStream.setAttr("fields", c.getAttr("fields"));
  newUnionStream.setAttr("functions", ::mlir::ArrayAttr::get(functionNames, c.getContext()));
  return newUnionStream.getResult();
}

Value flattenCallsLambda(sexpr::CombineOp& c, OpBuilder& builder, FuncOp function) {
  static int generatedFuncs = 0;
  auto sOrVType = c.getType().dyn_cast_or_null<SymbolOrValueType>();
  if (!sOrVType) {
    throw std::runtime_error("Expecting symbol Lambda type to be wrapped in sOrVType");
  }
  auto type = sOrVType.getBaseType().dyn_cast_or_null<GenericTupleStreamUnionType>();
  if (!type) {
    throw std::runtime_error("Expecting symbol Lambda to return union of functions");
  }

  std::vector<::mlir::Attribute> functionNames;
  for (auto const& childType : type.getChildren()) {
    auto savedInsertionPoint = builder.saveInsertionPoint();
    builder.setInsertionPointToStart(function.getParentOfType<ModuleOp>().getBody());
    auto funcType = childType.dyn_cast_or_null<FunctionType>();
    if (!funcType) {
      throw std::runtime_error("Expecting symbol Lambda to return union of functions");
    }
    auto funcName = "lambda" + std::to_string(generatedFuncs++);
    auto childFunc = builder.create<FuncOp>(c.getLoc(), funcName, funcType);
    auto* funcBlock = childFunc.addEntryBlock();
    builder.setInsertionPointToStart(funcBlock);

    auto secondArg = *c.getRegion().getOps<sexpr::CombineOp>().begin()++;

    // For each input, lower the function body
    auto combineCopy = secondArg.clone();
    auto lastVal = flattenCallsRecursive(combineCopy, builder, childFunc);
    combineCopy.erase();
    // Create new returnOp
    builder.create<ReturnOp>(c.getLoc(), lastVal);
    builder.restoreInsertionPoint(savedInsertionPoint);

    functionNames.emplace_back(::mlir::StringAttr::get(funcName, c.getContext()));
  }

  auto newUnionStream = builder.create<sexpr::SymbolOp>(c.getLoc(), sOrVType, "Lambda", ::mlir::ValueRange());
  newUnionStream.setAttr("fields", c.getAttr("fields"));
  newUnionStream.setAttr("functions", ::mlir::ArrayAttr::get(functionNames, c.getContext()));
  return newUnionStream.getResult();
}

Value flattenCallsRecursive(sexpr::CombineOp& c, OpBuilder& builder, FuncOp function) {
  auto symbol = c.getHead();
  auto const& name = symbol.name().str();
  if (name == "Lambda") {
    return flattenCallsLambda(c, builder, function);
  }
  if (name == "Where") {
    return flattenCallsWhereSymbol(c, builder, function);
  }

  return flattenCallsStandard(c, builder, function);
}

struct SexprToFunctionsLoweringPass
    : public PassWrapper<SexprToFunctionsLoweringPass, OperationPass<ModuleOp>> {

  SexprToFunctionsLoweringPass(RuntimeTypes& returnType) : PassWrapper(), returnType(returnType) {}

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

  getOperation().dump();
}

}; // namespace

std::unique_ptr<mlir::Pass> createLowerToFunctionsPass(RuntimeTypes& returnType) {
  return std::make_unique<SexprToFunctionsLoweringPass>(returnType);
}
