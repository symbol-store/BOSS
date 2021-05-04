#include "Engines/MLIREngine/IR/MLIRGenerator.hpp"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseDialect.h"
#include "Expression.hpp"
#include "Utilities.hpp"
#include <mlir/Dialect/SCF/SCF.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/Function.h>
#include <stack>
#include <utility>

using namespace mlir;

MLIRGenerator::MLIRGenerator() : builder(&context) {
  context.getOrLoadDialect<mlir::sexpr::SExprDialect>();
  context.getOrLoadDialect<mlir::scf::SCFDialect>();
  context.getOrLoadDialect<mlir::memory::MemoryDialect>();
  context.getOrLoadDialect<mlir::database::DatabaseDialect>();

  theModule = mlir::ModuleOp::create(builder.getUnknownLoc());
  builder.clearInsertionPoint();
  builder.setInsertionPointToStart(theModule.getBody());
}

mlir::OwningModuleRef MLIRGenerator::generateModule(boss::Expression const& e) {
  visitExpression(e);

  return theModule;
}

void MLIRGenerator::visitComplexExpression(boss::ComplexExpression const& e) {
  auto op = builder.create<mlir::sexpr::CombineOp, mlir::Type>(
      builder.getUnknownLoc(),
      builder.getType<SymbolOrValueType, sexprtype::SymbolOrValue, llvm::Optional<Type>>(
          sexprtype::SymbolOrValue::UNKNOWN, llvm::Optional<Type>{}));

  auto saved = builder.saveInsertionPoint();
  // Creates block and sets new insertion point
  builder.createBlock(&op.expressions());

  // Push end marker (empty optional) to stack
  values.push(std::optional<mlir::Value>());

  for(auto it = e.getArguments().rbegin(); it != e.getArguments().rend(); it++) {
    visitExpression(*it);
  }

  auto head = e.getHead().getName();
  std::vector<mlir::Value> vs;
  for(int i = 0; !values.empty() && values.top().has_value(); i++) {
    vs.push_back(values.top().value());
    values.pop();
  }

  // Pop end marker off stack
  values.pop();

  auto headOp = builder.create<mlir::sexpr::SymbolOp, std::string&, mlir::ValueRange>(
      builder.getFileLineColLoc(Identifier::get(head, &context), 0, 0), head, mlir::ValueRange(vs));

  builder.create<mlir::sexpr::EndOp>(builder.getUnknownLoc(), headOp.getResult());

  values.push(op.getResult());

  // Restores old insertion point
  builder.restoreInsertionPoint(saved);
}

void MLIRGenerator::visitExpression(boss::Expression const& e) {
  std::visit(
      boss::utilities::overload(
          [&](int a) {
            auto newOp = builder.create<mlir::sexpr::IntegerConstantOp, int>(
                builder.getUnknownLoc(), int(a));
            values.push(newOp.getResult());
          },
          [&](char const* a) {
            auto newOp = builder.create<mlir::sexpr::StringConstantOp, std::string>(
                builder.getUnknownLoc(), std::string(a));

            values.push(newOp.getResult());
          },
          [&](boss::Symbol const& a) {
            auto op = builder.create<mlir::sexpr::SymbolOp, const std::string&, mlir::ValueRange>(
                builder.getUnknownLoc(), a.getName(), {});

            values.push(op.getResult());
          },
          [&](std::string const& a) {
            auto newOp = builder.create<mlir::sexpr::StringConstantOp, const std::string&>(
                builder.getUnknownLoc(), a);

            values.push(newOp.getResult());
          },
          [&](boss::ComplexExpression const& expression) { visitComplexExpression(expression); }),
      e);
}
