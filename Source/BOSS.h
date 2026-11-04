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
/**
 * Span constructors, one per element type. The elements of `data` are copied into the new span.
 *
 * Nulls in, nulls out: nothing is substituted for a null. No null pointer is ever read through.
 *
 * @param data the elements to copy. Null yields a null result. For the string and symbol
 *             constructors, a null *element* also yields a null result, since there is no null
 *             std::string or boss::Symbol to stand in for it.
 * @param size the number of elements in `data`; ignored when `data` is null.
 * @return a new span owned by the caller and released with freeBOSSExpressionSpan, or null as
 *         described above.
 */
struct BOSSExpressionSpan* makeBoolBOSSSpan(bool const* data, size_t size);
struct BOSSExpressionSpan* makeInt8BOSSSpan(int8_t const* data, size_t size);
struct BOSSExpressionSpan* makeInt32BOSSSpan(int32_t const* data, size_t size);
struct BOSSExpressionSpan* makeInt64BOSSSpan(int64_t const* data, size_t size);
struct BOSSExpressionSpan* makeFloatBOSSSpan(float const* data, size_t size);
struct BOSSExpressionSpan* makeDoubleBOSSSpan(double const* data, size_t size);
struct BOSSExpressionSpan* makeStringBOSSSpan(char const* const* data, size_t size);
struct BOSSExpressionSpan* makeSymbolBOSSSpan(char const* const* data, size_t size);
/**
 * The address of the span's first element, as an integer, for verifying that a payload crossed
 * a boundary without being copied.
 *
 * The return type is size_t rather than uintptr_t because the chibi binding generator
 * understands size_t; the two coincide on every platform BOSS targets.
 *
 * @param span the span to inspect; may be null.
 * @return the address of the first element, or 0 when there is none -- for a null `span`, and
 *         for spans that are not contiguously addressable, notably bool spans (backed by
 *         std::vector<bool>). A 0 therefore means "no address available", not "empty span".
 */
size_t getBOSSSpanBeginAddress(struct BOSSExpressionSpan const* span);
/**
 * Builds a new complex expression combining dynamic arguments and span arguments.
 *
 * @param head the head symbol; cloned, so the caller keeps ownership.
 * @param cardinality the number of entries in `arguments`.
 * @param arguments cloned, so the caller retains ownership and must still free them itself
 *                  (e.g. via freeBOSSExpression), just as with newComplexBOSSExpression.
 * @param spanCount the number of entries in `spans`.
 * @param spans NOT cloned: each span's payload is moved (zero-copy) out of the wrapper into the
 *              new expression, leaving spans[i] valid and empty. The caller must not read from
 *              spans[i] afterwards, but is still responsible for freeing the (now-empty)
 *              wrapper via freeBOSSExpressionSpan, otherwise the wrapper allocation leaks.
 *              A null array contributes no span arguments whatever `spanCount` says, and a
 *              null entry within the array is skipped, so no null pointer is read through.
 * @return the new expression, owned by the caller.
 */
struct BOSSExpression* newComplexBOSSExpressionWithSpans(struct BOSSSymbol* head,
                                                         size_t cardinality,
                                                         struct BOSSExpression* arguments[],
                                                         size_t spanCount,
                                                         struct BOSSExpressionSpan* spans[]);

size_t getDynamicArgumentCountFromBOSSExpression(struct BOSSExpression const* arg);
/**
 * The dynamic arguments of a complex expression, as clones. Same contract as
 * getArgumentsFromBOSSExpression.
 *
 * @param arg the expression to read; left untouched.
 * @return a newly allocated, null-terminated array of clones, owned by the caller and released
 *         with freeBOSSArguments, which frees both the expressions and the array.
 */
struct BOSSExpression** getDynamicArgumentsFromBOSSExpression(struct BOSSExpression const* arg);
size_t getSpanArgumentCountFromBOSSExpression(struct BOSSExpression const* arg);
/**
 * Destructively retrieves the span arguments of a complex expression.
 *
 * @param arg the expression to take the spans from. Each span is moved (zero-copy) out of it,
 *            so its span-argument count becomes 0 and any spans it held must no longer be
 *            accessed through it.
 * @return a newly allocated array of newly allocated spans, owned by the caller, which must
 *         free each span via freeBOSSExpressionSpan and then the array via freeBOSSSpanArray.
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
