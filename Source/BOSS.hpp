#pragma once
#include "Engine.hpp"
#include "Expression.hpp"

extern "C" {
struct BOSSSymbol {
  boss::Symbol delegate;
};
BOSSSymbol* symbolNameToNewBOSSSymbol(char const* name);
char const* symbolToNewString(BOSSSymbol const* arg);

struct BOSSExpression {
  boss::Expression delegate;
};
BOSSExpression* longToNewBOSSExpression(int64_t value);
BOSSExpression* doubleToNewBOSSExpression(double value);
BOSSExpression* stringToNewBOSSExpression(char const* string);
BOSSExpression* symbolNameToNewBOSSExpression(char const* name);

BOSSExpression* newComplexBOSSExpression(BOSSSymbol* head, size_t cardinality,
                                         BOSSExpression* arguments[]);

/**
 *     bool = 0, long = 1, double = 2 , std::string = 3, Symbol = 4 , ComplexExpression = 5
 */
size_t getBOSSExpressionTypeID(BOSSExpression const* arg);

bool getBoolValueFromBOSSExpression(BOSSExpression const* arg);
std::int64_t getLongValueFromBOSSExpression(BOSSExpression const* arg);
std::double_t getDoubleValueFromBOSSExpression(BOSSExpression const* arg);
char* getNewStringValueFromBOSSExpression(BOSSExpression const* arg);
char const* getNewSymbolNameFromBOSSExpression(BOSSExpression const* arg);

BOSSSymbol* getHeadFromBOSSExpression(BOSSExpression const* arg);
size_t getArgumentCountFromBOSSExpression(BOSSExpression const* arg);
BOSSExpression** getArgumentsFromBOSSExpression(BOSSExpression const* arg);

BOSSExpression* BOSSEvaluate(BOSSExpression const* arg);
void freeBOSSExpression(BOSSExpression* expression);
void freeBOSSArguments(BOSSExpression** arguments);
void freeBOSSSymbol(BOSSSymbol* symbol);
void freeBOSSString(char* string);
}

namespace boss {
expressions::Expression evaluate(expressions::Expression const& expr);
}
