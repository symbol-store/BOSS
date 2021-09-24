#include <array>
#include <catch2/catch.hpp>

#include "../Source/BOSS.hpp"

TEST_CASE("Build Expression", "[api]") {
  auto input = (std::array{intToNewBOSSExpression(3), intToNewBOSSExpression(4)});
  auto* s = symbolNameToNewBOSSSymbol("Plus");
  auto* c = newComplexBOSSExpression(s, 2, input.data());
  auto* res = BOSSEvaluate(c);
  auto result = getIntValueFromBOSSExpression(res);
  freeBOSSExpression(c);
  freeBOSSSymbol(s);
  freeBOSSExpression(res);
  freeBOSSExpression(input[0]);
  freeBOSSExpression(input[1]);
  CHECK(result == 7);
}
