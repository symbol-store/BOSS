#pragma once

#include "Expression.hpp"
#include <stdint.h>

struct SExpression;

union SExpressionArgument {
  SExpression* args;
  int64_t value;
};

struct SExpression {
  char* head;
  SExpressionArgument* args;
};

extern "C" SExpression* allocateSymbol(char* name);

boss::Expression mExpressionFromSExpression(SExpression* expr);
