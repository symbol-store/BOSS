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
      : mlirContext(mlirContext), database(database), activePartitions(std::move(openRelations)),
        symbolOp(symbolOp) {}

  // The MLIR context
  ::mlir::MLIRContext* mlirContext;
  // The global database
  new_runtime::Database const* database;
  // The current fields that may exist from relations
  std::vector<std::map<std::string, ::mlir::Type>> activePartitions;
  // The current symbols that may otherwise be defined
  std::map<std::string, ::mlir::Type> symbolTable;
  // The current symbol op being processed
  ::mlir::sexpr::SymbolOp* symbolOp;
  // The symbols that are arguments to the current function
  std::vector<std::string> argumentSymbols;
};

::mlir::Type inferSymbolType(std::string const& symbolName, const std::vector<::mlir::Type>& argTypes,
                             TypeInferenceContext& context);

void updateContext(::mlir::sexpr::SymbolOp* symbolOp, TypeInferenceContext& context);
} // namespace boss::mlir::inference