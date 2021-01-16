#include <functional>
#include <map>
#include <stdarg.h>
#include <stdexcept>
#include <stdlib.h>

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

SymbolArgumentType llvmTypeToRuntimeArgType(mlir::LLVM::LLVMType type) {
  if(type.isIntegerTy(1)) {
    return SymbolArgumentType::Bool;
  } else if(type.isIntegerTy()) {
    return SymbolArgumentType::Int;
  } else if(type.isFloatTy()) {
    return SymbolArgumentType::Float;
  } else if(type.isPointerTy()) {
    auto elementType =
        type.cast<mlir::LLVM::LLVMPointerType>().getElementType().cast<mlir::LLVM::LLVMType>();

    if(elementType.isIntegerTy(8)) {
      return SymbolArgumentType::String;
    } else if(elementType.isStructTy()) {
      return SymbolArgumentType::Symbol;
    } else {
      throw std::runtime_error("Unknown LLVM type");
    }
    return SymbolArgumentType::Symbol;
  } else {
    throw std::runtime_error("Unknown LLVM type");
  }
}
