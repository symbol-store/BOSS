#include <functional>
#include <iostream>
#include <map>
#include <stdarg.h>
#include <stdexcept>
#include <stdlib.h>

#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include <mlir/IR/StandardTypes.h>

#include "Runtime.hpp"

using namespace boss::mlir::types;

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
                             va_arg(argValuesAndTypes, RuntimeTypes)};
  }

  va_end(argValuesAndTypes);

  baseExpr->argc = argc;
  baseExpr->arguments = args;
}
