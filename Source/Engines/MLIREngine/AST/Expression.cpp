#include "Engines/MLIREngine/AST/Expression.hpp"

namespace mlirengine {
void CombineExpression::accept(ExpressionVisitor& v) { v.visit(*this); }

void IntegerLiteralExpression::accept(ExpressionVisitor& v) { v.visit(*this); }

void SymbolExpression::accept(ExpressionVisitor& v) { v.visit(*this); }

void StringLiteralExpression::accept(ExpressionVisitor& v) { v.visit(*this); }
} // namespace mlirengine
