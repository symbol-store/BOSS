#ifndef PORTABLEBOSSSERIALIZATION_H
#define PORTABLEBOSSSERIALIZATION_H
#ifdef __cplusplus
extern "C" {
#endif
// NOLINTBEGIN(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

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
  return (struct PortableBossArgument*)root->arguments;
}

static struct PortableBossExpression*
getExpressionSubexpressions(struct PortableBOSSExpressionRoot* root) {

  return (struct PortableBossExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[root->argumentCount];
}

static struct PortableBOSSExpressionRoot* allocateExpressionTree(uint64_t argumentCount,
                                                                 uint64_t expressionCount) {
  struct PortableBOSSExpressionRoot* root =
      (struct PortableBOSSExpressionRoot*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      malloc(                              // NOLINT(hicpp-no-malloc,cppcoreguidelines-no-malloc)
          sizeof(argumentCount) + sizeof(expressionCount) +
          sizeof(struct PortableBossArgument) * argumentCount +
          sizeof(struct PortableBossExpression) * expressionCount);
  *((uint64_t*)&root->argumentCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount;
  *((uint64_t*)&root->expressionCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      expressionCount;
  return root;
}

static void freeExpressionTree(struct PortableBOSSExpressionRoot* root) {
  free(root); // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
}

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
  auto STRING = PortableBossArgument::SymbolType::STRING;
#endif
  buffer[argumentOutputI].type = STRING;
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

struct PortableBOSSExpressionRoot* serializeBOSSExpression(struct BOSSExpression*);
struct BOSSExpression* deserializeBOSSExpression(struct PortableBOSSExpressionRoot*);
struct BOSSExpression* parseURL(char const* url);

#ifdef __cplusplus
}
#endif
// NOLINTEND(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

#endif /* PORTABLEBOSSSERIALIZATION_H */
