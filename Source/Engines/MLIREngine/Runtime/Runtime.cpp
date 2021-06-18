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
using boss::mlir::runtime::string::RuntimeString;

extern "C" SymbolExpression* allocateSymbol(RuntimeString* name) {
  SymbolExpression* newMemory = (SymbolExpression*)malloc(sizeof(SymbolExpression));

  auto string = static_cast<char*>(malloc(name->length + 1));

  newMemory->head = string;
  newMemory->argc = 0;
  newMemory->arguments = nullptr;

  newMemory->head[name->length] = '\0';
  memcpy(newMemory->head, name->data, name->length);

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
