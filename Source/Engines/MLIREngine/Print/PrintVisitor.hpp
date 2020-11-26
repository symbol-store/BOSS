#pragma once
#include "Engines/MLIREngine/AST/Expression.hpp"
#include "Engines/MLIREngine/AST/Visitor.hpp"

// Prints the expression to console.
class PrintVisitor : public ExpressionVisitor {
public:
  void visit(mlirengine::CombineExpression& e) override;
  void visit(mlirengine::IntegerLiteralExpression& e) override;
  void visit(mlirengine::SymbolExpression& e) override;
  void visit(mlirengine::StringLiteralExpression& e) override;
};
