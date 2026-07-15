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
char const* bossSymbolToNewString(struct BOSSSymbol const* arg);

struct BOSSExpression;
struct BOSSExpression* boolToNewBOSSExpression(bool value);
struct BOSSExpression* charToNewBOSSExpression(int8_t value);
struct BOSSExpression* intToNewBOSSExpression(int32_t value);
struct BOSSExpression* longToNewBOSSExpression(int64_t value);
struct BOSSExpression* floatToNewBOSSExpression(float value);
struct BOSSExpression* doubleToNewBOSSExpression(double value);
struct BOSSExpression* stringToNewBOSSExpression(char const* string);
struct BOSSExpression* symbolNameToNewBOSSExpression(char const* name);

struct BOSSExpression* newComplexBOSSExpression(struct BOSSSymbol* head, size_t cardinality,
                                                struct BOSSExpression* arguments[]);

struct BOSSExpressionSpan;
struct BOSSExpressionSpan* makeBoolBOSSSpan(bool const* data, size_t size);
struct BOSSExpressionSpan* makeInt8BOSSSpan(int8_t const* data, size_t size);
struct BOSSExpressionSpan* makeInt32BOSSSpan(int32_t const* data, size_t size);
struct BOSSExpressionSpan* makeInt64BOSSSpan(int64_t const* data, size_t size);
struct BOSSExpressionSpan* makeFloatBOSSSpan(float const* data, size_t size);
struct BOSSExpressionSpan* makeDoubleBOSSSpan(double const* data, size_t size);
struct BOSSExpressionSpan* makeStringBOSSSpan(char const* const* data, size_t size);
struct BOSSExpressionSpan* makeSymbolBOSSSpan(char const* const* data, size_t size);
size_t getBOSSSpanBeginAddress(struct BOSSExpressionSpan const* span);
/**
 * Build a new complex expression combining dynamic arguments and span arguments.
 * `arguments` are cloned: the caller retains ownership of `arguments` and must still free
 * them itself (e.g. via freeBOSSExpression), just as with newComplexBOSSExpression.
 * `spans` are NOT cloned: each span's payload is moved (zero-copy) out of the wrapper into
 * the new expression, leaving spans[i] valid but empty. The caller must not read from
 * spans[i] afterwards, but is still responsible for freeing the (now-empty) wrapper itself
 * via freeBOSSExpressionSpan, otherwise the wrapper allocation leaks.
 */
struct BOSSExpression* newComplexBOSSExpressionWithSpans(struct BOSSSymbol* head,
                                                         size_t cardinality,
                                                         struct BOSSExpression* arguments[],
                                                         size_t spanCount,
                                                         struct BOSSExpressionSpan* spans[]);

size_t getDynamicArgumentCountFromBOSSExpression(struct BOSSExpression const* arg);
struct BOSSExpression** getDynamicArgumentsFromBOSSExpression(struct BOSSExpression const* arg);
size_t getSpanArgumentCountFromBOSSExpression(struct BOSSExpression const* arg);
/**
 * Destructively retrieves the span arguments of a complex expression: each span is moved
 * (zero-copy) out of `arg`, so `arg`'s span-argument count becomes 0 afterwards and any
 * spans it held must no longer be accessed through `arg`. The returned array and each span
 * it points to are newly allocated and owned by the caller, which must free the individual
 * spans via freeBOSSExpressionSpan and then the array itself via freeBOSSSpanArray.
 */
struct BOSSExpressionSpan** getSpanArgumentsFromBOSSExpression(struct BOSSExpression* arg);
/** Frees only the array returned by getSpanArgumentsFromBOSSExpression, not its elements;
 *  free each element individually with freeBOSSExpressionSpan first. */
void freeBOSSSpanArray(struct BOSSExpressionSpan** array);
void freeBOSSExpressionSpan(struct BOSSExpressionSpan* span);

/**
 *  bool = 0, char = 1, int = 2, long = 3, float = 4, double = 5, std::string = 6, Symbol = 7,
 *  ComplexExpression = 8
 */
size_t getBOSSExpressionTypeID(struct BOSSExpression const* arg);

bool getBoolValueFromBOSSExpression(struct BOSSExpression const* arg);
int8_t getCharValueFromBOSSExpression(struct BOSSExpression const* arg);
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
