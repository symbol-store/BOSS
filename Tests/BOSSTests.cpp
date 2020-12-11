#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#ifdef WSINTERFACE
#include "../Source/BOSS.hpp"
#include "../Source/Utilities.hpp"
using std::get;
using std::string;
using boss::utilities::operator""_;

TEMPLATE_TEST_CASE("Simpletons", "", boss::engines::wolfram::Engine) { // NOLINT
  static auto engine = TestType();
  SECTION("Basics") {
    REQUIRE(get<int>(engine.evaluate("Plus"_(5, 4))) == 9);
    REQUIRE(get<int>(engine.evaluate("Plus"_(5, 2, 2))) == 9);
    REQUIRE(get<int>(engine.evaluate("Plus"_(5, 2, 2))) == 9);
    REQUIRE(get<int>(engine.evaluate("Plus"_("Plus"_(2, 3), 2, 2))) == 9);
    REQUIRE(get<int>(engine.evaluate("Plus"_("Plus"_(3, 2), 2, 2))) == 9);
    REQUIRE(get<string>(engine.evaluate("StringJoin"_("howdie", " ", "world"))) == "howdie world");
    REQUIRE(get<bool>(engine.evaluate("Greater"_(5, 2))));
    REQUIRE(!get<bool>(engine.evaluate("Greater"_(2, 5))));
    REQUIRE(get<boss::Expression::Symbol>(engine.evaluate("Symbol"_("x"))).getName() == "x");
    REQUIRE(get<boss::Expression>(engine.evaluate("UndefinedFunction"_(9))).getHead() ==
            "UndefinedFunction");
  }
}
#endif // WSINTERFACE
