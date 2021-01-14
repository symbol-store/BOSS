#include "Runtime.hpp"

#include <stdlib.h>

extern "C" SExpression* allocateSymbol(char* name) {
  SExpression* newMemory = (SExpression*)malloc(sizeof(SExpression));

  newMemory->head = name;
  newMemory->args = nullptr;

  // TODO Reference counting/garbage collection
  return newMemory;
}
