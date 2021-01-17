#include <functional>
#include <iostream>
#include <map>
#include <stdarg.h>
#include <stdexcept>
#include <stdlib.h>

#include "Engines/MLIREngine/Dialect/SExprTypes.h"
#include <mlir/IR/StandardTypes.h>

#include "Runtime.hpp"

extern "C" SymbolExpression* allocateSymbol(char* name) {
  SymbolExpression* newMemory = (SymbolExpression*)malloc(sizeof(SymbolExpression));

  newMemory->head = name;
  newMemory->argc = 0;
  newMemory->arguments = nullptr;

  // TODO Reference counting/garbage collection
  return newMemory;
}

extern "C" void setSExpressionArgs(SymbolExpression* baseExpr, int64_t argc, ...) {
  SymbolArgument* args = (SymbolArgument*)malloc(sizeof(SymbolArgument) * argc);
  va_list argValuesAndTypes;
  va_start(argValuesAndTypes, argc);

  for(int i = 0; i < argc; i++) {
    args[i] = SymbolArgument{va_arg(argValuesAndTypes, SymbolArgumentValue),
                             va_arg(argValuesAndTypes, SymbolArgumentType)};
  }

  va_end(argValuesAndTypes);

  baseExpr->argc = argc;
  baseExpr->arguments = args;
}

SymbolArgumentType llvmTypeToRuntimeArgType(mlir::Type type) {
  if(type.isInteger(1)) {
    return SymbolArgumentType::Bool;
  } else if(type.isIntOrIndex()) {
    return SymbolArgumentType::Int;
  } else if(type.isIntOrFloat()) {
    return SymbolArgumentType::Float;
  } else if(type.isa<mlir::MemRefType>()) {
    return SymbolArgumentType::String;
  } else if(type.isa<SymbolOrValueType>()) {
    if(type.cast<SymbolOrValueType>().isSymbolic() == sexprtype::SymbolOrValue::SYMBOL) {
      return SymbolArgumentType::Symbol;
    } else {
      throw std::runtime_error("Unknwon Type");
    }
  } else {
    throw std::runtime_error("Unknown type");
  }
}
