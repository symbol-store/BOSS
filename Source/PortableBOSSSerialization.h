#ifndef PORTABLEBOSSSERIALIZATION_H
#define PORTABLEBOSSSERIALIZATION_H
#ifdef __cplusplus
#include <cinttypes>
#include <cstring>
extern "C" {
#else
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#endif
// NOLINTBEGIN(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

#include <stdlib.h>

//////////////////////////////// Helper Functions ///////////////////////////////

static uint64_t alignTo8Bytes(uint64_t bytes) { return (bytes + (uint64_t)7) & -(uint64_t)8; }

//////////////////////////////// Data Structures ///////////////////////////////

typedef size_t PortableBOSSString;
typedef size_t PortableBOSSExpressionIndex;

union PortableBOSSArgumentValue {
  bool asBool;
  int8_t asChar;
  int16_t asShort;
  int32_t asInt;
  int64_t asLong;
  float asFloat;
  double asDouble;
  PortableBOSSString asString;
  PortableBOSSExpressionIndex asExpression;
};

#ifdef __cplusplus
constexpr uint64_t PortableBOSSArgument_BOOL_SIZE = sizeof(bool);
constexpr uint64_t PortableBOSSArgument_CHAR_SIZE = sizeof(int8_t);
constexpr uint64_t PortableBOSSArgument_SHORT_SIZE = sizeof(int16_t);
constexpr uint64_t PortableBOSSArgument_INT_SIZE = sizeof(int32_t);
constexpr uint64_t PortableBOSSArgument_LONG_SIZE = sizeof(int64_t);
constexpr uint64_t PortableBOSSArgument_FLOAT_SIZE = sizeof(float_t);
constexpr uint64_t PortableBOSSArgument_DOUBLE_SIZE = sizeof(double_t);
constexpr uint64_t PortableBOSSArgument_STRING_SIZE = sizeof(PortableBOSSString);
constexpr uint64_t PortableBOSSArgument_EXPRESSION_SIZE = sizeof(PortableBOSSExpressionIndex);
#else
static uint64_t const PortableBOSSArgument_BOOL_SIZE = sizeof(bool);
static uint64_t const PortableBOSSArgument_CHAR_SIZE = sizeof(int8_t);
static uint64_t const PortableBOSSArgument_SHORT_SIZE = sizeof(int16_t);
static uint64_t const PortableBOSSArgument_INT_SIZE = sizeof(int32_t);
static uint64_t const PortableBOSSArgument_LONG_SIZE = sizeof(int64_t);
static uint64_t const PortableBOSSArgument_FLOAT_SIZE = sizeof(float_t);
static uint64_t const PortableBOSSArgument_DOUBLE_SIZE = sizeof(double_t);
static uint64_t const PortableBOSSArgument_STRING_SIZE = sizeof(PortableBOSSString);
static uint64_t const PortableBOSSArgument_EXPRESSION_SIZE = sizeof(PortableBOSSExpressionIndex);
#endif

enum PortableBOSSArgumentType : uint8_t {
  ARGUMENT_TYPE_BOOL,
  ARGUMENT_TYPE_CHAR,
  ARGUMENT_TYPE_SHORT,
  ARGUMENT_TYPE_INT,
  ARGUMENT_TYPE_LONG,
  ARGUMENT_TYPE_FLOAT,
  ARGUMENT_TYPE_DOUBLE,
  ARGUMENT_TYPE_STRING,
  ARGUMENT_TYPE_SYMBOL,
  ARGUMENT_TYPE_EXPRESSION
};

static uint8_t const PortableBOSSArgumentType_RLE_MINIMUM_SIZE =
    13; // assuming PortableBOSSArgumentType ideally stored in 1 byte only,
        // to store RLE-type, need 1 byte to declare the type and 4 bytes to define the length

static uint8_t const PortableBOSSArgumentType_RLE_BIT =
    0x80; // first bit of PortableBOSSArgumentType to set RLE on/off

static uint8_t const PortableBOSSArgumentType_MASK =
    0x0F; // used to clear the top 4 bits of an argument type

struct PortableBOSSExpression {
  uint64_t symbolNameOffset;
  uint64_t startChildOffset;     // Arg buffer offset
  uint64_t endChildOffset;       // Arg buffer offset
  uint64_t startChildTypeOffset; // Type buffer offset
  uint64_t endChildTypeOffset;   // Type buffer offset
};

/**
 * A single-allocation representation of an expression, including its arguments (i.e., a flattened
 * array of all arguments, another flattened array of argument types and an array of
 * PortableExpressions to encode the structure)
 */
struct PortableBOSSRootExpression {
  uint64_t const argumentCount;      // if used directly for type bytes -- align to 8 bytes
  uint64_t const argumentBytesCount; // if used directly -- align to 8 bytes
  uint64_t const expressionCount;
  uint64_t const argumentDictionaryBytesCount;
  void* const originalAddress;
  /**
   * The index of the last used byte in the arguments buffer relative to the pointer returned by
   * getStringBuffer()
   */
  size_t stringArgumentsFillIndex;

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
      &root->arguments[alignTo8Bytes(root->argumentBytesCount)];
}

static struct PortableBOSSExpression*
getExpressionSubexpressions(struct PortableBOSSRootExpression* root) {
  return (struct PortableBOSSExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[alignTo8Bytes(root->argumentBytesCount) +
                       alignTo8Bytes(root->argumentCount * sizeof(enum PortableBOSSArgumentType))];
}

static char* getStringBuffer(struct PortableBOSSRootExpression* root) {
  return (char*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[alignTo8Bytes(root->argumentBytesCount) +
                       alignTo8Bytes(root->argumentCount * sizeof(enum PortableBOSSArgumentType)) +
                       root->expressionCount * (sizeof(struct PortableBOSSExpression)) +
                       root->argumentDictionaryBytesCount];
}

//////////////////////////////   Memory Management /////////////////////////////

static struct PortableBOSSRootExpression*
allocateExpressionTree(uint64_t argumentCount, uint64_t expressionCount, uint64_t stringBytesCount,
                       void* (*allocateFunction)(size_t)) {
  struct PortableBOSSRootExpression* root =
      (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      allocateFunction(                    // NOLINT(hicpp-no-malloc,cppcoreguidelines-no-malloc)
          sizeof(struct PortableBOSSRootExpression) +
          sizeof(union PortableBOSSArgumentValue) * argumentCount +
          alignTo8Bytes(sizeof(enum PortableBOSSArgumentType) * argumentCount) +
          sizeof(struct PortableBOSSExpression) * expressionCount + stringBytesCount);
  *((uint64_t*)&root->argumentCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount;
  *((uint64_t*)&root->argumentBytesCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount * sizeof(union PortableBOSSArgumentValue);
  *((uint64_t*)&root->expressionCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      expressionCount;
  *((uint64_t*)&root
        ->argumentDictionaryBytesCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((uint64_t*)&root->stringArgumentsFillIndex) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((void**)&root->originalAddress) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      root;
  return root;
}

static struct PortableBOSSRootExpression*
allocateExpressionTree(uint64_t argumentCount, uint64_t argumentBytesCount,
                       uint64_t expressionCount, uint64_t stringBytesCount,
                       void* (*allocateFunction)(size_t)) {
  struct PortableBOSSRootExpression* root =
      (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      allocateFunction(                    // NOLINT(hicpp-no-malloc,cppcoreguidelines-no-malloc)
          sizeof(struct PortableBOSSRootExpression) + alignTo8Bytes(argumentBytesCount) +
          alignTo8Bytes(sizeof(enum PortableBOSSArgumentType) * argumentCount) +
          sizeof(struct PortableBOSSExpression) * expressionCount + stringBytesCount);
  *((uint64_t*)&root->argumentCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount;
  *((uint64_t*)&root->argumentBytesCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentBytesCount;
  *((uint64_t*)&root->expressionCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      expressionCount;
  *((uint64_t*)&root
        ->argumentDictionaryBytesCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((uint64_t*)&root->stringArgumentsFillIndex) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((void**)&root->originalAddress) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      root;
  return root;
}

static void freeExpressionTree(struct PortableBOSSRootExpression* root,
                               void (*freeFunction)(void*)) {
  freeFunction(root); // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
}

static uint64_t* makeArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {

  return (uint64_t*)&getExpressionArguments(root)[argumentOutputI];
};

static bool* makeBoolArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_BOOL = PortableBOSSArgumentType::ARGUMENT_TYPE_BOOL;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_BOOL;
  return &getExpressionArguments(root)[argumentOutputI].asBool;
};

static bool* makeBoolArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                              uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_BOOL = PortableBOSSArgumentType::ARGUMENT_TYPE_BOOL;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_BOOL;
  return &getExpressionArguments(root)[argumentOutputI].asBool;
};

static void makeBoolArgumentType(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_BOOL = PortableBOSSArgumentType::ARGUMENT_TYPE_BOOL;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_BOOL;
};

static int8_t* makeCharArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_CHAR = PortableBOSSArgumentType::ARGUMENT_TYPE_CHAR;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_CHAR;
  return &getExpressionArguments(root)[argumentOutputI].asChar;
};

static int8_t* makeCharArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_CHAR = PortableBOSSArgumentType::ARGUMENT_TYPE_CHAR;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_CHAR;
  return &getExpressionArguments(root)[argumentOutputI].asChar;
};

static void makeCharArgumentType(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_CHAR = PortableBOSSArgumentType::ARGUMENT_TYPE_CHAR;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_CHAR;
};

static int16_t* makeShortArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SHORT = PortableBOSSArgumentType::ARGUMENT_TYPE_SHORT;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SHORT;
  return &getExpressionArguments(root)[argumentOutputI].asShort;
};

static int16_t* makeShortArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                  uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SHORT = PortableBOSSArgumentType::ARGUMENT_TYPE_SHORT;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_SHORT;
  return &getExpressionArguments(root)[argumentOutputI].asShort;
};

static void makeShortArgumentType(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SHORT = PortableBOSSArgumentType::ARGUMENT_TYPE_SHORT;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SHORT;
};

static int32_t* makeIntArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_INT = PortableBOSSArgumentType::ARGUMENT_TYPE_INT;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_INT;
  return &getExpressionArguments(root)[argumentOutputI].asInt;
};

static int32_t* makeIntArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_INT = PortableBOSSArgumentType::ARGUMENT_TYPE_INT;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_INT;
  return &getExpressionArguments(root)[argumentOutputI].asInt;
};

static void makeIntArgumentType(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_INT = PortableBOSSArgumentType::ARGUMENT_TYPE_INT;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_INT;
};

static int64_t* makeLongArgument(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_LONG = PortableBOSSArgumentType::ARGUMENT_TYPE_LONG;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_LONG;
  return &getExpressionArguments(root)[argumentOutputI].asLong;
};

static int64_t* makeLongArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                 uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_LONG = PortableBOSSArgumentType::ARGUMENT_TYPE_LONG;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_LONG;
  return &getExpressionArguments(root)[argumentOutputI].asLong;
};

static void makeLongArgumentType(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_LONG = PortableBOSSArgumentType::ARGUMENT_TYPE_LONG;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_LONG;
};

static float* makeFloatArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_FLOAT = PortableBOSSArgumentType::ARGUMENT_TYPE_FLOAT;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_FLOAT;
  return &getExpressionArguments(root)[argumentOutputI].asFloat;
};

static float* makeFloatArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_FLOAT = PortableBOSSArgumentType::ARGUMENT_TYPE_FLOAT;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_FLOAT;
  return &getExpressionArguments(root)[argumentOutputI].asFloat;
};

static void makeFloatArgumentType(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_FLOAT = PortableBOSSArgumentType::ARGUMENT_TYPE_FLOAT;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_FLOAT;
};

static double* makeDoubleArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_DOUBLE = PortableBOSSArgumentType::ARGUMENT_TYPE_DOUBLE;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_DOUBLE;
  return &getExpressionArguments(root)[argumentOutputI].asDouble;
};

static double* makeDoubleArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                  uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_DOUBLE = PortableBOSSArgumentType::ARGUMENT_TYPE_DOUBLE;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_DOUBLE;
  return &getExpressionArguments(root)[argumentOutputI].asDouble;
};

static void makeDoubleArgumentType(struct PortableBOSSRootExpression* root,
                                   uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_DOUBLE = PortableBOSSArgumentType::ARGUMENT_TYPE_DOUBLE;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_DOUBLE;
};

static size_t* makeStringArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_STRING = PortableBOSSArgumentType::ARGUMENT_TYPE_STRING;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_STRING;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static size_t* makeStringArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                  uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_STRING = PortableBOSSArgumentType::ARGUMENT_TYPE_STRING;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_STRING;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static void makeStringArgumentType(struct PortableBOSSRootExpression* root,
                                   uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_STRING = PortableBOSSArgumentType::ARGUMENT_TYPE_STRING;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_STRING;
};

static size_t* makeSymbolArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_SYMBOL;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SYMBOL;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static size_t* makeSymbolArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                  uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_SYMBOL;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_SYMBOL;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static void makeSymbolArgumentType(struct PortableBOSSRootExpression* root,
                                   uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_SYMBOL;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SYMBOL;
};

static size_t* makeExpressionArgument(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_EXPRESSION;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_EXPRESSION;
  return &getExpressionArguments(root)[argumentOutputI].asExpression;
};

static size_t* makeExpressionArgument(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_EXPRESSION;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_EXPRESSION;
  return &getExpressionArguments(root)[argumentOutputI].asExpression;
};

static void setRLEArgumentFlagOrPropagateTypes(struct PortableBOSSRootExpression* root,
                                               uint64_t argumentOutputI, uint32_t size) {
  if(size < PortableBOSSArgumentType_RLE_MINIMUM_SIZE) {
    // RLE is not supported, fallback to set the argument types
    enum PortableBOSSArgumentType const type = getArgumentTypes(root)[argumentOutputI];
    for(uint64_t i = argumentOutputI + 1; i < argumentOutputI + size; ++i) {
      getArgumentTypes(root)[i] = type;
    }
    return;
  }
  PortableBOSSArgumentType* argTypes = getArgumentTypes(root);
  (*(uint8_t*)(&argTypes[argumentOutputI])) |=
      PortableBOSSArgumentType_RLE_BIT; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
  (*(uint8_t*)(&argTypes[argumentOutputI + 4])) =
      (size >> 24) & 0xFF; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
  (*(uint8_t*)(&argTypes[argumentOutputI + 3])) =
      (size >> 16) & 0xFF; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
  (*(uint8_t*)(&argTypes[argumentOutputI + 2])) =
      (size >> 8) & 0xFF; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
  (*(uint8_t*)(&argTypes[argumentOutputI + 1])) =
      size & 0xFF; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
}

static int8_t* makeCharArgumentsRun(struct PortableBOSSRootExpression* root,
                                    uint64_t argumentOutputI, uint32_t size) {
  int8_t* value = makeCharArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static int16_t* makeShortArgumentsRun(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint32_t size) {
  int16_t* value = makeShortArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static int32_t* makeIntArgumentsRun(struct PortableBOSSRootExpression* root,
                                    uint64_t argumentOutputI, uint32_t size) {
  int32_t* value = makeIntArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static int64_t* makeLongArgumentsRun(struct PortableBOSSRootExpression* root,
                                     uint64_t argumentOutputI, uint32_t size) {
  int64_t* value = makeLongArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static float* makeFloatArgumentsRun(struct PortableBOSSRootExpression* root,
                                    uint64_t argumentOutputI, uint64_t size) {
  float* value = makeFloatArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static double* makeDoubleArgumentsRun(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint64_t size) {
  double* value = makeDoubleArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static size_t* makeStringArgumentsRun(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint64_t size) {
  size_t* value = makeStringArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static size_t* makeSymbolArgumentsRun(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint32_t size) {
  size_t* value = makeSymbolArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static size_t* makeExpressionArgumentsRun(struct PortableBOSSRootExpression* root,
                                          uint64_t argumentOutputI, uint64_t size) {
  size_t* value = makeExpressionArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static struct PortableBOSSExpression* makeExpression(struct PortableBOSSRootExpression* root,
                                                     uint64_t expressionOutputI) {
  return &getExpressionSubexpressions(root)[expressionOutputI];
}

static size_t storeString(struct PortableBOSSRootExpression** root, char const* inputString) {
  size_t const inputStringLength = strlen(inputString);
  char const* result = strncpy(getStringBuffer(*root) + (*root)->stringArgumentsFillIndex,
                               inputString, inputStringLength + 1);
  (*root)->stringArgumentsFillIndex += inputStringLength + 1;
  return result - getStringBuffer(*root);
};

static size_t storeStringReallocation(struct PortableBOSSRootExpression** root,
                                      char const* inputString,
                                      void* (*reallocateFunction)(void*, size_t)) {
  size_t const inputStringLength = strlen(inputString);
  *root = (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      reallocateFunction(*root, // NOLINT(hicpp-no-malloc, cppcoreguidelines-no-malloc)
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
