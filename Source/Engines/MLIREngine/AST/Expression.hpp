#pragma once
#include "Engines/MLIREngine/AST/Visitor.hpp"
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Class hierarchy to represent basic s-expressions
namespace mlirengine {
// Base Expression
class Expression {
public:
  virtual void accept(ExpressionVisitor& v) = 0;
  virtual ~Expression() = default;

  Expression() = default;
  Expression(const Expression& copyFrom) = default;
  Expression& operator=(const Expression& copyFrom) = default;
  Expression(Expression&&) = default;
  Expression& operator=(Expression&&) = default;
};

// Expression representing list of expressions
class CombineExpression : public Expression {
public:
  CombineExpression() = default;

  template <typename... Exprs> explicit CombineExpression(Exprs&&... exprs) {
    addToElems(std::forward<Exprs>(exprs)...);
  }

  std::vector<std::unique_ptr<Expression>> args;

  void accept(ExpressionVisitor& v) override;

private:
  // Initialize args from template
  template <typename T> void addToElems(T&& item) {
    args.push_back(std::make_unique<T>(std::forward<T>(item)));
  }
  // Initialize args from template recursively
  template <typename T, typename... Ts> void addToElems(T&& item, Ts&&... items) {
    args.push_back(std::make_unique<T>(std::forward<T>(item)));
    addToElems(std::forward<Ts>(items)...);
  }
};

// Expression representing integer value
class IntegerLiteralExpression : public Expression {
public:
  explicit IntegerLiteralExpression(int64_t value) : value(value) {}
  int64_t value;

  void accept(ExpressionVisitor& v) override;
};

// Expression representing symbol (function or constant)
class SymbolExpression : public Expression {
public:
  explicit SymbolExpression(const std::string&& symbol)
      : symbol(std::forward<const std::string>(symbol)) {}
  std::string symbol;

  void accept(ExpressionVisitor& v) override;
};

// Expression representing string literal
class StringLiteralExpression : public Expression {
public:
  explicit StringLiteralExpression(const std::string& value) : value(value) {}
  explicit StringLiteralExpression(const std::string&& value) : value(std::move(value)) {}
  std::string value;

  void accept(ExpressionVisitor& v) override;
};

}; // namespace mlirengine
