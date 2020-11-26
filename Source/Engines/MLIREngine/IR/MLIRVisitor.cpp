#include "Engines/MLIREngine/IR/MLIRVisitor.hpp"
#include "Engines/MLIREngine/Dialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprTypes.h"
#include "Engines/MLIREngine/IR/MLIRVisitor.hpp"
#include "mlir/IR/Builders.h"
#include "mlir/IR/RegionKindInterface.h"
#include "mlir/IR/Verifier.h"
#include <cassert>
#include <climits>
#include <iostream>
#include <mlir/Dialect/SCF/SCF.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/IR/Function.h>
#include <stack>
#include <utility>

// TODO: Create a map from operator to arity, types, etc.
std::map<std::string, int64_t> MLIRVisitor::operandArity = {{"+", 10000},  // NOLINT
                                                            {"eval", 1},   // NOLINT
                                                            {"-", 10000},  // NOLINT
                                                            {"*", 10000},  // NOLINT
                                                            {"/", 10000}}; // NOLINT

using namespace mlir;

MLIRVisitor::MLIRVisitor() : builder(&context) {
  context.getOrLoadDialect<mlir::sexpr::SExprDialect>();
  context.getOrLoadDialect<scf::SCFDialect>();

  theModule = mlir::ModuleOp::create(builder.getUnknownLoc());
  builder.clearInsertionPoint();
  builder.setInsertionPointToStart(theModule.getBody());
}

void MLIRVisitor::visit(mlirengine::CombineExpression& e) {
  auto op = builder.create<mlir::sexpr::CombineOp, mlir::Type>(
      builder.getUnknownLoc(), builder.getType<SymbolOrValueType, sexprtype::SymbolOrValue, Type>(
                                   sexprtype::SymbolOrValue::UNKNOWN, builder.getI64Type()));

  auto saved = builder.saveInsertionPoint();
  // Creates block and sets new insertion point
  builder.createBlock(&op.expressions());

  // Push end marker (empty optional) to stack
  values.push(std::optional<mlir::Value>());

  // Visit all children
  for(auto it = e.args.rbegin(); it != e.args.rend(); it++) {
    (*it)->accept(*this);
  }

  auto result = values.top().value();
  values.pop();

  builder.create<mlir::sexpr::EndOp>(builder.getUnknownLoc(), result);

  values.push(op.getResult());

  // Restores old insertion point
  builder.restoreInsertionPoint(saved);
}

void MLIRVisitor::visit(mlirengine::IntegerLiteralExpression& e) {
  auto newOp =
      builder.create<mlir::sexpr::IntegerConstantOp, int>(builder.getUnknownLoc(), int(e.value));

  values.push(newOp.getResult());
}

void MLIRVisitor::visit(mlirengine::SymbolExpression& e) {

  std::vector<mlir::Value> vs;
  for(int i = 0; i < operandArity[e.symbol] && !values.empty() && values.top().has_value(); i++) {
    vs.push_back(values.top().value());
    values.pop();
  }

  // Pop end marker off stack
  values.pop();

  auto op = builder.create<mlir::sexpr::SymbolOp, std::string, mlir::ValueRange>(
      builder.getUnknownLoc(), std::string(e.symbol), mlir::ValueRange(vs));

  values.push(op.getResult());
}

void MLIRVisitor::visit(mlirengine::StringLiteralExpression& e) {
  auto newOp = builder.create<mlir::sexpr::StringConstantOp, std::string>(builder.getUnknownLoc(),
                                                                          std::string(e.value));

  values.push(newOp.getResult());
}

mlir::OwningModuleRef MLIRVisitor::getModule() {
  if(mlir::failed(mlir::verify(theModule))) {
    theModule.emitError("Failed to verify module");
    return nullptr;
  }

  return theModule;
}
