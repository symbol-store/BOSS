#ifndef PORTABLEBOSSSERIALIZATION_H
#define PORTABLEBOSSSERIALIZATION_H
#ifdef __cplusplus
extern "C" {
#include <cinttypes>
#else
#include <inttypes.h>
#endif
// NOLINTBEGIN(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

#include <stdlib.h>

//////////////////////////////// Data Structures ///////////////////////////////

union PortableBOSSArgumentValue {
  int64_t asLong;
  double asDouble;
  char* asString;
};

enum PortableBOSSArgumentType { LONG, DOUBLE, STRING, SYMBOL };

struct PortableBOSSExpression {
  uint64_t headOffset;
  uint64_t firstChildOffset;
  uint64_t lastChildOffset;
};

/**
 * A single-allocation representation of an expression, including its arguments (i.e., a flattened
 * array of all arguments, another flattened array of argument types and an array of
 * PortableExpressions to encode the structure)
 */
struct PortableBOSSRootExpression {
  uint64_t const argumentCount;
  uint64_t const expressionCount;

  /**
   * This buffer holds all data associated with the expression in a single untyped array. As the
   * three kinds of data (ArgumentsValues, ArgumentTypes and Expressions) have different sizes,
   * holding them in an array of unions would waste a lot of memory. A union of variable-sized arrays
   * is not supported in ANSI C. So it is held in an untyped buffer which is essentially a
   * concatenation of the three types of buffers that are required. Utility functions exist to
   * extract the different sub-arrays.
   */
  char arguments[];
};

//////////////////////////////// Part Extraction ///////////////////////////////

struct PortableBOSSRootExpression* getDummySerializedExpression();
static union PortableBOSSArgumentValue* getExpressionArguments(struct PortableBOSSRootExpression* root) {
  return (union PortableBOSSArgumentValue*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      root->arguments;
}

static enum PortableBOSSArgumentType* getArgumentTypes(struct PortableBOSSRootExpression* root) {
  return (enum PortableBOSSArgumentType*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[root->argumentCount * sizeof(union PortableBOSSArgumentValue)];
}

static struct PortableBOSSExpression*
getExpressionSubexpressions(struct PortableBOSSRootExpression* root) {
  return (struct PortableBOSSExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[root->argumentCount *
                       (sizeof(union PortableBOSSArgumentValue) + sizeof(enum PortableBOSSArgumentType))];
}

//////////////////////////////   Memory Management /////////////////////////////

static struct PortableBOSSRootExpression* allocateExpressionTree(uint64_t argumentCount,
                                                                 uint64_t expressionCount) {
  struct PortableBOSSRootExpression* root =
      (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      malloc(                              // NOLINT(hicpp-no-malloc,cppcoreguidelines-no-malloc)
          sizeof(argumentCount) + sizeof(expressionCount) +
          sizeof(union PortableBOSSArgumentValue) * argumentCount +
          sizeof(PortableBOSSArgumentType) * argumentCount +
          sizeof(struct PortableBOSSExpression) * expressionCount);
  *((uint64_t*)&root->argumentCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount;
  *((uint64_t*)&root->expressionCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      expressionCount;
  return root;
}

static void freeExpressionTree(struct PortableBOSSRootExpression* root) {
  free(root); // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
}

////////////////////////////// Argument Modifiers //////////////////////////////

static int64_t* makeLongArgument(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto LONG = PortableBOSSArgumentType::LONG;
#endif

  getArgumentTypes(root)[argumentOutputI] = LONG;
  return &getExpressionArguments(root)[argumentOutputI].asLong;
};

static char** makeSymbolArgument(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto SYMBOL = PortableBOSSArgumentType::SYMBOL;
#endif
  getArgumentTypes(root)[argumentOutputI] = SYMBOL;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static char** makeStringArgument(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto STRING = PortableBOSSArgumentType::STRING;
#endif
  getArgumentTypes(root)[argumentOutputI] = STRING;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static double* makeDoubleArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto DOUBLE = PortableBOSSArgumentType::DOUBLE;
#endif
  getArgumentTypes(root)[argumentOutputI] = DOUBLE;
  return &getExpressionArguments(root)[argumentOutputI].asDouble;
};

static struct PortableBOSSExpression* makeExpression(struct PortableBOSSExpression* expressions,
                                                     uint64_t expressionOutputI) {
  return &expressions[expressionOutputI];
}

struct PortableBOSSRootExpression* serializeBOSSExpression(struct BOSSExpression* expression);
struct BOSSExpression* deserializeBOSSExpression(struct PortableBOSSRootExpression* root);
struct BOSSExpression* parseURL(char const* url);

#ifdef __cplusplus
}
#endif
// NOLINTEND(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

#endif /* PORTABLEBOSSSERIALIZATION_H */
