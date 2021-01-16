#pragma once

#include <mlir/Dialect/LLVMIR/LLVMTypes.h>

enum class SymbolArgumentType { Int = 0, Bool = 1, Float, String, Symbol };

struct SymbolExpression;

union SymbolArgumentValue {
  int integerValue;
  bool booleanValue;
  float floatValue;
  char* stringValue;
  SymbolExpression* symbolValue;
};

struct SymbolArgument {
  SymbolArgument(SymbolArgumentValue value, SymbolArgumentType type) : type(type), value(value) {}

  SymbolArgumentType type;
  SymbolArgumentValue value;
};

struct SymbolExpression {
  char* head;
  int64_t argc;
  SymbolArgument* arguments;
};

extern "C" SymbolExpression* allocateSymbol(char* name);

SymbolArgumentType llvmTypeToRuntimeArgType(mlir::LLVM::LLVMType type);
