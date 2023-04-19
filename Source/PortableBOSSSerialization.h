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
  uint64_t argumentCount;
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
      sizeof(argumentCount) + sizeof(struct PortableBossArgument) * argumentCount +
      sizeof(struct PortableBossExpression) * expressionCount);
  root->argumentCount = argumentCount;
  return root;
}

static void freeExpressionTree(struct PortableBOSSExpressionRoot* root) { free(root); }

static int64_t* makeLongArgument(struct PortableBossArgument* buffer, uint64_t argumentOutputI) {
  buffer[argumentOutputI].type = PortableBossArgument::SymbolType::LONG;
  return &buffer[argumentOutputI].asLong;
};

static char** makeSymbolArgument(struct PortableBossArgument* buffer, uint64_t argumentOutputI) {
  buffer[argumentOutputI].type = PortableBossArgument::SymbolType::SYMBOL;
  return &buffer[argumentOutputI].asString;
};

static double* makeDoubleArgument(struct PortableBossArgument* buffer, uint64_t argumentOutputI) {
  buffer[argumentOutputI].type = PortableBossArgument::SymbolType::DOUBLE;
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
