#include "Engines/MLIREngine/Print/PrintVisitor.hpp"
#include <iostream>

void PrintVisitor::visit(mlirengine::CombineExpression& e) {
  std::cout << "( ";
  for(const auto& child : e.args) {
    child->accept(*this);
  }
  std::cout << ") ";
}

void PrintVisitor::visit(mlirengine::IntegerLiteralExpression& e) { std::cout << e.value << " "; }

void PrintVisitor::visit(mlirengine::SymbolExpression& e) { std::cout << e.symbol << " "; }

void PrintVisitor::visit(mlirengine::StringLiteralExpression& e) { std::cout << e.value << " "; }
