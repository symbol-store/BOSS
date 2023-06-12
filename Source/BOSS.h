#ifndef BOSS_H
#define BOSS_H
#ifdef __cplusplus
#include <cinttypes>
#include <cstddef>
extern "C" {
#else
#include <inttypes.h>
#include <stdbool.h>
#endif

struct BOSSSymbol;
struct BOSSSymbol* symbolNameToNewBOSSSymbol(char const* name);
char const* symbolToNewString(struct BOSSSymbol const* arg);

struct BOSSExpression;
struct BOSSExpression* longToNewBOSSExpression(int64_t value);
struct BOSSExpression* doubleToNewBOSSExpression(double value);
struct BOSSExpression* stringToNewBOSSExpression(char const* string);
struct BOSSExpression* symbolNameToNewBOSSExpression(char const* name);

struct BOSSExpression* newComplexBOSSExpression(struct BOSSSymbol* head, size_t cardinality,
                                                struct BOSSExpression* arguments[]);

/**
 *     bool = 0, long = 1, double = 2 , std::string = 3, Symbol = 4 , ComplexExpression = 5
 */
size_t getBOSSExpressionTypeID(struct BOSSExpression const* arg);

bool getBoolValueFromBOSSExpression(struct BOSSExpression const* arg);
int64_t getLongValueFromBOSSExpression(struct BOSSExpression const* arg);
double getDoubleValueFromBOSSExpression(struct BOSSExpression const* arg);
char* getNewStringValueFromBOSSExpression(struct BOSSExpression const* arg);
char const* getNewSymbolNameFromBOSSExpression(struct BOSSExpression const* arg);

struct BOSSSymbol* getHeadFromBOSSExpression(struct BOSSExpression const* arg);
size_t getArgumentCountFromBOSSExpression(struct BOSSExpression const* arg);
struct BOSSExpression** getArgumentsFromBOSSExpression(struct BOSSExpression const* arg);

struct BOSSExpression* BOSSEvaluate(struct BOSSExpression const* arg);
void freeBOSSExpression(struct BOSSExpression* expression);
void freeBOSSArguments(struct BOSSExpression** arguments);
void freeBOSSSymbol(struct BOSSSymbol* symbol);
void freeBOSSString(char* string);
#ifdef __cplusplus
}
#endif

#endif /* BOSS_H */
