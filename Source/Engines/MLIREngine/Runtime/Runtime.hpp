#pragma once

struct SExpression {
  char* head;
  SExpression* args;
};

extern "C" SExpression* allocateSymbol(char* name);
