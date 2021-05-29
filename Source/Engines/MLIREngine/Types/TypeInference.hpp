#pragma once

#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include <Utilities.hpp>
#include <map>
#include <mlir/IR/Types.h>
#include <string>
#include <utility>

namespace boss::mlir::inference {
bool isRegisteredSymbol(std::string const& symbol);
bool hasSymbolicArguments(std::vector<::mlir::Type> const& arguments);

struct TypeInferenceContext {
  TypeInferenceContext(::mlir::MLIRContext* mlirContext, const new_runtime::Database* database,
                       std::vector<std::map<std::string, ::mlir::Type>> openRelations,
                       ::mlir::sexpr::SymbolOp* symbolOp)
      : mlirContext(mlirContext), database(database), activePartitions(std::move(openRelations)),
        symbolOp(symbolOp) {}

  TypeInferenceContext(::mlir::MLIRContext* mlirContext, const new_runtime::Database* database,
                       std::unordered_map<std::string, boss::Expression> symbolTable)
      : mlirContext(mlirContext), database(database) {
    for(auto const& symbol : symbolTable) {
      auto name = symbol.first;
      std::visit(
          boss::utilities::overload(
              [&](int e) { this->symbolTable[name] = ::mlir::IntegerType::get(32, mlirContext); },
              [&](size_t e) {
                this->symbolTable[name] = ::mlir::IntegerType::get(64, mlirContext);
              },
              [&](bool e) { this->symbolTable[name] = ::mlir::IntegerType::get(1, mlirContext); },
              [&](char const* e) { this->symbolTable[name] = StringType::get(mlirContext); },
              [&](std::string e) { this->symbolTable[name] = StringType::get(mlirContext); },
              [&](float e) { this->symbolTable[name] = ::mlir::Float32Type::get(mlirContext); },
              [&](Symbol e) {
                this->symbolTable[name] =
                    SymbolOrValueType::get(mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
              },
              [&](ComplexExpression e) {
                this->symbolTable[name] =
                    SymbolOrValueType::get(mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
              }),
          symbol.second);
    }
  }

  // The MLIR context
  ::mlir::MLIRContext* mlirContext;
  // The global database
  new_runtime::Database const* database;
  // Relation and Field Name
  std::shared_ptr<arrow::Array> currentArray;
  // The current fields that may exist from relations
  std::vector<std::map<std::string, ::mlir::Type>> activePartitions;
  // The current symbols that may otherwise be defined
  std::map<std::string, ::mlir::Type> symbolTable;
  // The current symbol op being processed
  ::mlir::sexpr::SymbolOp* symbolOp;
  // The symbols that are arguments to the current function
  std::vector<std::string> argumentSymbols;
};

::mlir::Type inferSymbolType(std::string const& symbolName,
                             const std::vector<::mlir::Type>& argTypes,
                             TypeInferenceContext& context);

void updateContext(::mlir::sexpr::SymbolOp* symbolOp, TypeInferenceContext& context);
} // namespace boss::mlir::inference