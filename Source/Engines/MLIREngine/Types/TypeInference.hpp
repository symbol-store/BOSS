#pragma once

#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include <Utilities.hpp>
#include <map>
#include <mlir/IR/Types.h>
#include <string>
#include <utility>

namespace boss::mlir::conversion {
::mlir::Type stringToMLIRType(::mlir::MLIRContext* context, std::string typeName);
}

namespace boss::mlir::inference {
bool isRegisteredSymbol(std::string const& symbol);
bool hasSymbolicArguments(std::vector<::mlir::Type> const& arguments);

struct TypeInferenceContext;

::mlir::Type inferSymbolType(std::string const& symbolName,
                             const std::vector<::mlir::Type>& argTypes,
                             TypeInferenceContext& context);

struct TypeInferenceContext {
  TypeInferenceContext(::mlir::MLIRContext* mlirContext, const new_runtime::Database* database,
                       std::vector<std::map<std::string, ::mlir::Type>> openRelations,
                       ::mlir::sexpr::SymbolOp* symbolOp)
      : mlirContext(mlirContext), database(database), activePartitions(std::move(openRelations)),
        symbolOp(symbolOp) {}

  static ::mlir::Type expressionToType(boss::Expression const& e, ::mlir::MLIRContext* mlirContext) {
    ::mlir::Type result;

    std::visit(
        boss::utilities::overload(
            [&](int e) { result = ::mlir::IntegerType::get(32, mlirContext); },
            [&](size_t e) {
              result = ::mlir::IntegerType::get(64, mlirContext);
            },
            [&](bool e) { result = ::mlir::IntegerType::get(1, mlirContext); },
            [&](char const* e) { result = StringType::get(mlirContext); },
            [&](std::string e) { result = StringType::get(mlirContext); },
            [&](float e) { result = ::mlir::Float32Type::get(mlirContext); },
            [&](Symbol e) {
              result =
                  SymbolOrValueType::get(mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
            },
            [&](ComplexExpression e) {
              // TODO we can't use the normal type inference here, because that uses the context..
              if (e.getHead().getName() == "NextValue") {
                result = boss::mlir::conversion::stringToMLIRType(mlirContext, std::get<boss::Symbol>(e.getArguments()[1]).getName());
              } else {
                std::vector<::mlir::Type> argTypes;
                for (auto const& child : e.getArguments()) {
                  argTypes.emplace_back(expressionToType(child, mlirContext));
                }

                auto childContext = TypeInferenceContext(mlirContext, nullptr, {}, nullptr);
                auto inferredType = inferSymbolType(e.getHead().getName(), argTypes, childContext);

                // Extract the symbolOrValue if it is a value
                if (inferredType.isa<SymbolOrValueType>() && inferredType.dyn_cast<SymbolOrValueType>().isSymbolic() == sexprtype::SymbolOrValue::VALUE) {
                  result = inferredType.dyn_cast<SymbolOrValueType>().getBaseType();
                }
              }

            }),
        e);

    return result;
  }

  TypeInferenceContext(::mlir::MLIRContext* mlirContext, const new_runtime::Database* database,
                       std::unordered_map<std::string, boss::Expression> symbolTable)
      : mlirContext(mlirContext), database(database) {
    for(auto const& symbol : symbolTable) {
      auto name = symbol.first;

      this->symbolTable[name] = expressionToType(symbol.second, mlirContext);
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

void updateContext(::mlir::sexpr::SymbolOp* symbolOp, TypeInferenceContext& context);
} // namespace boss::mlir::inference