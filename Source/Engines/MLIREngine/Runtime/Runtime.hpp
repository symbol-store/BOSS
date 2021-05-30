#pragma once

#include <mlir/IR/Types.h>
#include "Engines/MLIREngine/Types/Types.hpp"
#include "Strings.hpp"

struct SymbolExpression;

union SymbolArgumentValue {
  int integerValue;
  bool booleanValue;
  float floatValue;
  boss::mlir::runtime::string::RuntimeString* stringValue;
  SymbolExpression* symbolValue;
};

struct SymbolArgument {
  SymbolArgument(SymbolArgumentValue value, boss::mlir::types::RuntimeTypes type) : type(type), value(value) {}

  boss::mlir::types::RuntimeTypes type;
  SymbolArgumentValue value;
};

struct SymbolExpression {
  char* head;
  int64_t argc;
  SymbolArgument* arguments;
};

extern "C" SymbolExpression* allocateSymbol(boss::mlir::runtime::string::RuntimeString* name);
