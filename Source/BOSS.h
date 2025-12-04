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
struct BOSSExpression* boolToNewBOSSExpression(bool value);
struct BOSSExpression* charToNewBOSSExpression(int8_t value);
struct BOSSExpression* shortToNewBOSSExpression(int16_t value);
struct BOSSExpression* intToNewBOSSExpression(int32_t value);
struct BOSSExpression* longToNewBOSSExpression(int64_t value);
struct BOSSExpression* floatToNewBOSSExpression(float value);
struct BOSSExpression* doubleToNewBOSSExpression(double value);
struct BOSSExpression* stringToNewBOSSExpression(char const* string);
struct BOSSExpression* symbolNameToNewBOSSExpression(char const* name);

struct BOSSExpression* newComplexBOSSExpression(struct BOSSSymbol* head, size_t cardinality,
                                                struct BOSSExpression* arguments[]);

/**
 *  bool = 0, char = 1, short = 2, int = 3, long = 4, float = 5, double = 6, std::string = 7, Symbol
 * = 8, ComplexExpression = 9
 */
size_t getBOSSExpressionTypeID(struct BOSSExpression const* arg);

bool getBoolValueFromBOSSExpression(struct BOSSExpression const* arg);
int8_t getCharValueFromBOSSExpression(struct BOSSExpression const* arg);
int16_t getShortValueFromBOSSExpression(struct BOSSExpression const* arg);
int32_t getIntValueFromBOSSExpression(struct BOSSExpression const* arg);
int64_t getLongValueFromBOSSExpression(struct BOSSExpression const* arg);
float getFloatValueFromBOSSExpression(struct BOSSExpression const* arg);
double getDoubleValueFromBOSSExpression(struct BOSSExpression const* arg);
char* getNewStringValueFromBOSSExpression(struct BOSSExpression const* arg);
char const* getNewSymbolNameFromBOSSExpression(struct BOSSExpression const* arg);

struct BOSSSymbol* getHeadFromBOSSExpression(struct BOSSExpression const* arg);
size_t getArgumentCountFromBOSSExpression(struct BOSSExpression const* arg);
struct BOSSExpression** getArgumentsFromBOSSExpression(struct BOSSExpression const* arg);

struct BOSSExpression* BOSSEvaluate(struct BOSSExpression* arg);
void freeBOSSExpression(struct BOSSExpression* expression);
void freeBOSSArguments(struct BOSSExpression** arguments);
void freeBOSSSymbol(struct BOSSSymbol* symbol);
void freeBOSSString(char* string);
#ifdef __cplusplus
}
#endif

#endif /* BOSS_H */
