#include "Engines/MLIREngine/IR/MLIRGenerator.hpp"
#include "Engines/MLIREngine/Dialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprTypes.h"
#include <mlir/Dialect/SCF/SCF.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/Function.h>
#include <mlir/IR/RegionKindInterface.h>
#include <mlir/IR/Verifier.h>
#include <stack>
#include <tuple>
#include <utility>

template <class... Fs> struct overload : Fs... {
  template <class... Ts> explicit overload(Ts&&... ts) : Fs{std::forward<Ts>(ts)}... {}
  using Fs::operator()...;
};

template <class... Ts> overload(Ts&&...)->overload<std::remove_reference_t<Ts>...>;

MLIRGenerator::MLIRGenerator() : builder(&context) {
  context.getOrLoadDialect<mlir::sexpr::SExprDialect>();
  context.getOrLoadDialect<scf::SCFDialect>();

  theModule = mlir::ModuleOp::create(builder.getUnknownLoc());
  builder.clearInsertionPoint();
  builder.setInsertionPointToStart(theModule.getBody());
}

mlir::OwningModuleRef MLIRGenerator::generateModule(Expression const& e) {
  visitExpression(e);

  return theModule;
}

void MLIRGenerator::visitExpression(Expression const& e) {
  auto op = builder.create<mlir::sexpr::CombineOp, mlir::Type>(
      builder.getUnknownLoc(), builder.getType<SymbolOrValueType, sexprtype::SymbolOrValue, Type>(
                                   sexprtype::SymbolOrValue::UNKNOWN, builder.getI64Type()));

  auto saved = builder.saveInsertionPoint();
  // Creates block and sets new insertion point
  builder.createBlock(&op.expressions());

  // Push end marker (empty optional) to stack
  values.push(std::optional<mlir::Value>());

  for(auto it = e.getArguments().rbegin(); it != e.getArguments().rend(); it++) {
    std::visit(
        overload(
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
            [&](Expression::Symbol const& a) {
              auto op = builder.create<mlir::sexpr::SymbolOp, const std::string&, mlir::ValueRange>(
                  builder.getUnknownLoc(), a.getName(), {});

              values.push(op.getResult());
            },
            [&](std::string const& a) {
              auto newOp = builder.create<mlir::sexpr::StringConstantOp, const std::string&>(
                  builder.getUnknownLoc(), a);

              values.push(newOp.getResult());
            },
            [&](Expression const& expression) { visitExpression(expression); }),
        *it);
  }

  auto head = e.getHead();
  std::vector<mlir::Value> vs;
  for(int i = 0; !values.empty() && values.top().has_value(); i++) {
    vs.push_back(values.top().value());
    values.pop();
  }

  // Pop end marker off stack
  values.pop();

  auto headOp = builder.create<mlir::sexpr::SymbolOp, std::string&, mlir::ValueRange>(
      builder.getUnknownLoc(), head, mlir::ValueRange(vs));

  builder.create<mlir::sexpr::EndOp>(builder.getUnknownLoc(), headOp.getResult());

  values.push(op.getResult());

  // Restores old insertion point
  builder.restoreInsertionPoint(saved);
}
