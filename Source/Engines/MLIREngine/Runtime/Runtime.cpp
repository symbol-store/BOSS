#include "Runtime.hpp"

#include <stdarg.h>
#include <stdlib.h>

extern "C" SExpression* allocateSymbol(char* name) {
  SExpression* newMemory = (SExpression*)malloc(sizeof(SExpression));

  newMemory->head = name;
  newMemory->args = nullptr;

  // TODO Reference counting/garbage collection
  return newMemory;
}

extern "C" void setSExpressionArgs(SExpression* baseExpr, int64_t argc, ...) {
  SExpressionArgument* args = (SExpressionArgument*)malloc(sizeof(SExpressionArgument) * argc);
  va_list argv;
  va_start(argv, argc);

  for(int i = 0; i < argc; i++) {
    args[i] = va_arg(argv, SExpressionArgument);
  }

  va_end(argv);

  baseExpr->args = args;
}

boss::Expression mExpressionFromSExpression(SExpression* expr) {
  if(expr->args == nullptr) {
    return boss::Symbol{expr->head};
  }
  // TODO correctly parse arguments
  return boss::ComplexExpression{boss::Symbol{expr->head}, {}};
}
