#pragma once
#include "Engines/MLIREngine/Types/Types.hpp"
#include "Expression.hpp"
#include <Engines/MLIREngine/Runtime/Runtime.hpp>
#include <mlir/IR/Types.h>

namespace boss::mlir::conversion {

boss::Expression mExpressionFromSExpression(SymbolExpression* expr);

} // namespace boss