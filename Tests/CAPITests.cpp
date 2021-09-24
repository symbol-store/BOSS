#include <array>
#include <catch2/catch.hpp>

#include "../Source/BOSS.hpp"

TEST_CASE("Build Expression", "[api]") {
  auto result = getIntValueFromBOSSExpression(BOSSEvaluate(newComplexBOSSExpression(
      symbolNameToNewBOSSSymbol("Plus"), 2,
      (std::array{intToNewBOSSExpression(3), intToNewBOSSExpression(4)}).data())));
  CHECK(result == 7);
}
