#pragma once

namespace mlirengine {
class Expression;
class CombineExpression;
class IntegerLiteralExpression;
class SymbolExpression;
class StringLiteralExpression;
} // namespace mlirengine

// Base class for expression visitors
class ExpressionVisitor {
public:
  virtual void visit(mlirengine::CombineExpression& e) = 0;
  virtual void visit(mlirengine::IntegerLiteralExpression& e) = 0;
  virtual void visit(mlirengine::SymbolExpression& e) = 0;
  virtual void visit(mlirengine::StringLiteralExpression& e) = 0;

  virtual ~ExpressionVisitor() = default;

  ExpressionVisitor() = default;
  ExpressionVisitor(const ExpressionVisitor& copyFrom) = delete;
  ExpressionVisitor& operator=(const ExpressionVisitor& copyFrom) = delete;
  ExpressionVisitor(ExpressionVisitor&&) = delete;
  ExpressionVisitor& operator=(ExpressionVisitor&&) = delete;
};
