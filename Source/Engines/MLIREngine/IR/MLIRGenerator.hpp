#pragma once

#include "Expression.hpp"
#include <map>
#include <mlir/IR/Builders.h>
#include <mlir/IR/Module.h>
#include <mlir/IR/RegionKindInterface.h>
#include <mlir/IR/Verifier.h>
#include <stack>

class MLIRGenerator {
public:
  MLIRGenerator();
  mlir::OwningModuleRef generateModule(boss::Expression const& e);

  MLIRGenerator(MLIRGenerator&& other) = default;
  MLIRGenerator(MLIRGenerator& other) = default;

private:
  mlir::MLIRContext context;
  mlir::OpBuilder builder;
  mlir::ModuleOp theModule;
  std::stack<std::optional<mlir::Value>> values;

  void visitExpression(boss::Expression const& e);
  void visitComplexExpression(boss::ComplexExpression const& e);
};
