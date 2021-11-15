#pragma once
#include "Engine.hpp"
#include "Expression.hpp"

extern "C" {
struct BOSSSymbol {
  boss::Symbol delegate;
};
BOSSSymbol* symbolNameToNewBOSSSymbol(char const* i);
char const* symbolToNewString(BOSSSymbol const* arg);

struct BOSSExpression {
  boss::Expression delegate;
};
BOSSExpression* intToNewBOSSExpression(int i);
BOSSExpression* floatToNewBOSSExpression(float i);
BOSSExpression* stringToNewBOSSExpression(char const* i);
BOSSExpression* symbolNameToNewBOSSExpression(char const* i);

BOSSExpression* newComplexBOSSExpression(BOSSSymbol* head, size_t cardinality,
                                         BOSSExpression* arguments[]);

/**
 *     bool = 0, int = 1, float = 2 , std::string = 3, Symbol = 4 , ComplexExpression = 5
 */
size_t getBOSSExpressionTypeID(BOSSExpression const* arg);

bool getBoolValueFromBOSSExpression(BOSSExpression const* arg);
int getIntValueFromBOSSExpression(BOSSExpression const* arg);
float getFloatValueFromBOSSExpression(BOSSExpression const* arg);
char const* getNewStringValueFromBOSSExpression(BOSSExpression const* arg);
char const* getNewSymbolNameFromBOSSExpression(BOSSExpression const* arg);

BOSSSymbol* getHeadFromBOSSExpression(BOSSExpression const* arg);
size_t getArgumentCountFromBOSSExpression(BOSSExpression const* arg);
BOSSExpression** getArgumentsFromBOSSExpression(BOSSExpression const* arg);

BOSSExpression* BOSSEvaluate(BOSSExpression const* arg);
void freeBOSSExpression(BOSSExpression* e);
void freeBOSSArguments(BOSSExpression** e);
void freeBOSSSymbol(BOSSSymbol* s);
}
