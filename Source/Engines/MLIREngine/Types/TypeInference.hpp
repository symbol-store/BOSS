#pragma once

#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include <string>

namespace boss::mlir::inference {
bool isRegisteredSymbol(std::string const& symbol);

::mlir::Type inferSymbolType(::mlir::sexpr::SymbolOp&, sexprtype::SymbolOrValue,
                             runtime::Database const&);
} // namespace boss::mlir::inference