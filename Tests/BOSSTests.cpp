#include "../Source/BOSS.hpp"
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#ifdef WSINTERFACE
TEMPLATE_TEST_CASE("Simpletons", "", boss::engines::wolfram::Engine) { // NOLINT
  using namespace std;
  static auto engine = TestType();
  SECTION("Basics") {
    REQUIRE(get<int>(engine.evaluate({"Plus", {5, 4}})) == 9);
    REQUIRE(get<int>(engine.evaluate({"Plus", {5, 2, 2}})) == 9);
    REQUIRE(get<int>(engine.evaluate({"Plus", {5, 2, 2}})) == 9);
    REQUIRE(get<int>(engine.evaluate({"Plus", {Expression{"Plus", {2, 3}}, 2, 2}})) == 9);
    REQUIRE(get<int>(engine.evaluate({"Plus", {Expression{"Plus", {3, 2}}, 2, 2}})) == 9);
    REQUIRE(get<string>(engine.evaluate({"StringJoin", {"howdie", " ", "world"}})) ==
            "howdie world");
    REQUIRE(get<bool>(engine.evaluate({"Greater", {5, 2}})));
    REQUIRE(!get<bool>(engine.evaluate({"Greater", {2, 5}})));
    REQUIRE(get<Expression::Symbol>(engine.evaluate({"Symbol", {"x"}})).getName() == "x");
    REQUIRE(get<Expression>(engine.evaluate({"UndefinedFunction", {9}})).getHead() == "UndefinedFunction");
  }
}
#endif // WSINTERFACE
