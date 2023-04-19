#ifndef PORTABLEBOSSSERIALIZATION_H
#define PORTABLEBOSSSERIALIZATION_H
#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdlib.h>

struct PortableBossArgument {
  enum SymbolType { LONG, DOUBLE, STRING, SYMBOL } type;
  union {
    int64_t asLong;
    double asDouble;
    char* asString;
  };
};

struct PortableBossExpression {
  uint64_t headOffset;
  uint64_t firstChildOffset;
  uint64_t lastChildOffset;
};

struct PortableBOSSExpressionRoot {
  uint64_t const argumentCount;
  uint64_t const expressionCount;
  struct PortableBossArgument arguments[];
};

struct PortableBOSSExpressionRoot* getDummySerializedExpression();
static struct PortableBossArgument*
getExpressionArguments(struct PortableBOSSExpressionRoot* root) {
  return root->arguments;
}

static struct PortableBossExpression*
getExpressionSubexpressions(struct PortableBOSSExpressionRoot* root) {

  return (struct PortableBossExpression*)&root->arguments[root->argumentCount];
}

static struct PortableBOSSExpressionRoot* allocateExpressionTree(uint64_t argumentCount,
                                                                 uint64_t expressionCount) {
  struct PortableBOSSExpressionRoot* root = (struct PortableBOSSExpressionRoot*)malloc(
      sizeof(argumentCount) + sizeof(expressionCount) + sizeof(struct PortableBossArgument) * argumentCount +
      sizeof(struct PortableBossExpression) * expressionCount);
  *((uint64_t*)&root->argumentCount) = argumentCount;
  *((uint64_t*)&root->expressionCount) = expressionCount;
  return root;
}

static void freeExpressionTree(struct PortableBOSSExpressionRoot* root) { free(root); }

static int64_t* makeLongArgument(struct PortableBossArgument* buffer, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto LONG = PortableBossArgument::SymbolType::LONG;
#endif

  buffer[argumentOutputI].type = LONG;
  return &buffer[argumentOutputI].asLong;
};

static char** makeSymbolArgument(struct PortableBossArgument* buffer, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto SYMBOL = PortableBossArgument::SymbolType::SYMBOL;
#endif
  buffer[argumentOutputI].type = SYMBOL;
  return &buffer[argumentOutputI].asString;
};

static char** makeStringArgument(struct PortableBossArgument* buffer, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto SYMBOL = PortableBossArgument::SymbolType::STRING;
#endif
  buffer[argumentOutputI].type = SYMBOL;
  return &buffer[argumentOutputI].asString;
};

static double* makeDoubleArgument(struct PortableBossArgument* buffer, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto DOUBLE = PortableBossArgument::SymbolType::DOUBLE;
#endif
  buffer[argumentOutputI].type = DOUBLE;
  return &buffer[argumentOutputI].asDouble;
};

static struct PortableBossExpression* makeExpression(struct PortableBossExpression* expressions,
                                                     uint64_t expressionOutputI) {
  return &expressions[expressionOutputI];
}

#ifdef __cplusplus
}
#endif
#endif /* PORTABLEBOSSSERIALIZATION_H */
