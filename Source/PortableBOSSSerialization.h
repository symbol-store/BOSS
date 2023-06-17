#ifndef PORTABLEBOSSSERIALIZATION_H
#define PORTABLEBOSSSERIALIZATION_H
#ifdef __cplusplus
#include <cinttypes>
#include <cstring>
extern "C" {
#else
#include <inttypes.h>
#endif
// NOLINTBEGIN(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

#include <stdlib.h>

//////////////////////////////// Data Structures ///////////////////////////////

typedef size_t PortableBOSSString;

union PortableBOSSArgumentValue {
  int64_t asLong = 0;
  double asDouble;
  PortableBOSSString asString;
};

enum PortableBOSSArgumentType : size_t {
  ARGUMENT_TYPE_LONG,
  ARGUMENT_TYPE_DOUBLE,
  ARGUMENT_TYPE_STRING,
  ARGUMENT_TYPE_SYMBOL
};

struct PortableBOSSExpression {
  uint64_t headOffset = 0;
  uint64_t firstChildOffset = 0;
  uint64_t lastChildOffset = 0;
};

/**
 * A single-allocation representation of an expression, including its arguments (i.e., a flattened
 * array of all arguments, another flattened array of argument types and an array of
 * PortableExpressions to encode the structure)
 */
struct PortableBOSSRootExpression {
  uint64_t const argumentCount = 0;
  uint64_t const expressionCount = 0;
  void* const originalAddress = nullptr;
  /**
   * The index of the last used byte in the arguments buffer relative to the pointer returned by
   * getStringBuffer()
   */
  size_t stringArgumentsFillIndex = 0;

  /**
   * This buffer holds all data associated with the expression in a single untyped array. As the
   * three kinds of data (ArgumentsValues, ArgumentTypes and Expressions) have different sizes,
   * holding them in an array of unions would waste a lot of memory. A union of variable-sized
   * arrays is not supported in ANSI C. So it is held in an untyped buffer which is essentially a
   * concatenation of the three types of buffers that are required. Utility functions exist to
   * extract the different sub-arrays.
   */
  char arguments[];
};

//////////////////////////////// Part Extraction ///////////////////////////////

struct PortableBOSSRootExpression* getDummySerializedExpression();
static union PortableBOSSArgumentValue*
getExpressionArguments(struct PortableBOSSRootExpression* root) {
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
      &root->arguments[root->argumentCount * (sizeof(union PortableBOSSArgumentValue) +
                                              sizeof(enum PortableBOSSArgumentType))];
}

static char* getStringBuffer(struct PortableBOSSRootExpression* root) {
  return (char*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[root->argumentCount * (sizeof(union PortableBOSSArgumentValue) +
                                              sizeof(enum PortableBOSSArgumentType)) +
                       root->expressionCount * (sizeof(PortableBOSSExpression))];
}

//////////////////////////////   Memory Management /////////////////////////////

static struct PortableBOSSRootExpression* allocateExpressionTree(uint64_t argumentCount,
                                                                 uint64_t expressionCount) {
  struct PortableBOSSRootExpression* root =
      (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      malloc(                              // NOLINT(hicpp-no-malloc,cppcoreguidelines-no-malloc)
          sizeof(struct PortableBOSSRootExpression) +
          sizeof(union PortableBOSSArgumentValue) * argumentCount +
          sizeof(PortableBOSSArgumentType) * argumentCount +
          sizeof(struct PortableBOSSExpression) * expressionCount);
  *((uint64_t*)&root->argumentCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount;
  *((uint64_t*)&root->expressionCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      expressionCount;
  *((uint64_t*)&root->stringArgumentsFillIndex) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((void**)&root->originalAddress) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      root;
  return root;
}

static void freeExpressionTree(struct PortableBOSSRootExpression* root) {
  free(root); // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
}

static int64_t* makeLongArgument(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_LONG = PortableBOSSArgumentType::ARGUMENT_TYPE_LONG;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_LONG;
  return &getExpressionArguments(root)[argumentOutputI].asLong;
};

static size_t* makeSymbolArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_SYMBOL;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SYMBOL;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static size_t* makeStringArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_STRING = PortableBOSSArgumentType::ARGUMENT_TYPE_STRING;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_STRING;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static double* makeDoubleArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_DOUBLE = PortableBOSSArgumentType::ARGUMENT_TYPE_DOUBLE;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_DOUBLE;
  return &getExpressionArguments(root)[argumentOutputI].asDouble;
};

static struct PortableBOSSExpression* makeExpression(struct PortableBOSSExpression* expressions,
                                                     uint64_t expressionOutputI) {
  return &expressions[expressionOutputI];
}

static size_t storeString(struct PortableBOSSRootExpression** root, char const* inputString) {
  size_t const inputStringLength = strlen(inputString);
  *root = (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      realloc(*root, // NOLINT(hicpp-no-malloc, cppcoreguidelines-no-malloc)
              ((char*)(getStringBuffer(*root)) -
               ((char*)*root)) + // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
                  (*root)->stringArgumentsFillIndex +
                  inputStringLength + 1);
  char const* result = strncpy(getStringBuffer(*root) + (*root)->stringArgumentsFillIndex,
                               inputString, inputStringLength + 1);
  (*root)->stringArgumentsFillIndex += inputStringLength + 1;
  return result - getStringBuffer(*root);
};

static char const* viewString(struct PortableBOSSRootExpression* root, size_t inputStringOffset) {
  return getStringBuffer(root) + inputStringOffset;
};

struct PortableBOSSRootExpression* serializeBOSSExpression(struct BOSSExpression* expression);
struct BOSSExpression* deserializeBOSSExpression(struct PortableBOSSRootExpression* root);
struct BOSSExpression* parseURL(char const* url);

#ifdef __cplusplus
}
#endif
// NOLINTEND(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

#endif /* PORTABLEBOSSSERIALIZATION_H */
