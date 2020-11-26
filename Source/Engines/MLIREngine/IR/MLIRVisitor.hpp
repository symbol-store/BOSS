#pragma once
#include "Engines/MLIREngine/AST/Expression.hpp"
#include "Engines/MLIREngine/AST/Visitor.hpp"
#include "Engines/MLIREngine/Dialect/SExprDialect.h"
#include <map>
#include <mlir/IR/Builders.h>
#include <mlir/IR/Module.h>
#include <optional>
#include <stack>

// Visitor to translate AST to MLIR module
class MLIRVisitor : public ExpressionVisitor {
public:
  MLIRVisitor();

  void visit(mlirengine::CombineExpression& e) override;
  void visit(mlirengine::IntegerLiteralExpression& e) override;
  void visit(mlirengine::SymbolExpression& e) override;
  void visit(mlirengine::StringLiteralExpression& e) override;

  mlir::OwningModuleRef getModule();

private:
  mlir::MLIRContext context;
  mlir::OpBuilder builder;
  mlir::ModuleOp theModule;
  std::stack<std::optional<mlir::Value>> values;
  static std::map<std::string, int64_t> operandArity;
};
