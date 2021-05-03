#pragma once

#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include <string>

namespace boss::mlir::inference {
bool isRegisteredSymbol(std::string const& symbol);

::mlir::Type inferSymbolType(::mlir::sexpr::SymbolOp&, sexprtype::SymbolOrValue,
                             new_runtime::Database const&);

::mlir::Type inferSymbolType(std::string symbolName, std::vector<::mlir::Type> const& argTypes, ::mlir::MLIRContext* context);
} // namespace boss::mlir::inference