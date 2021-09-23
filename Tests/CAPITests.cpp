#include <catch2/catch.hpp>

#include "../Source/BOSS.hpp"

TEST_CASE("Build Expression", "[api]") {
  auto result = getIntValueFromBOSSExpression(BOSSEvaluate(
      newComplexBOSSExpression(symbolNameToNewBOSSSymbol("Plus"), 2,
                               (BOSSExpression**)(BOSSExpression*[]){intToNewBOSSExpression(5),
                                                                     intToNewBOSSExpression(4)})));
  CHECK(result == 9);
}
