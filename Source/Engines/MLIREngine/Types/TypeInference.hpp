#pragma once

#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include <map>
#include <string>
#include <utility>

namespace boss::mlir::inference {
bool isRegisteredSymbol(std::string const& symbol);
bool hasSymbolicArguments(std::vector<::mlir::Type> const& arguments);

struct TypeInferenceContext {
  TypeInferenceContext(::mlir::MLIRContext* mlirContext, const new_runtime::Database* database,
                       std::vector<std::map<std::string, ::mlir::Type>>  openRelations,
                       ::mlir::sexpr::SymbolOp* symbolOp)
      : mlirContext(mlirContext), database(database), openRelations(std::move(openRelations)),
        symbolOp(symbolOp) {}

  ::mlir::MLIRContext* mlirContext;
  new_runtime::Database const* database;
  std::vector<std::map<std::string, ::mlir::Type>> openRelations;
  ::mlir::sexpr::SymbolOp* symbolOp;
  std::vector<std::string> argumentSymbols;
};

::mlir::Type inferSymbolType(std::string const& symbolName, std::vector<::mlir::Type>& argTypes,
                             TypeInferenceContext& context);
} // namespace boss::mlir::inference