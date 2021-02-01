#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#ifdef WSINTERFACE
#include "../Source/BOSS.hpp"
#include "../Source/Utilities.hpp"
using std::get;
using std::string;
using boss::utilities::operator""_;

TEMPLATE_TEST_CASE("Simpletons", "", boss::engines::wolfram::Engine) { // NOLINT
  static auto eval = [e = TestType()](boss::Expression const& expression) mutable {
    return e.evaluate(expression);
  };

  SECTION("Addition") {
    CHECK(get<int>(eval("Plus"_(5, 4))) == 9); // NOLINT
    CHECK(get<int>(eval("Plus"_(5, 2, 2))) == 9);
    CHECK(get<int>(eval("Plus"_(5, 2, 2))) == 9);
    CHECK(get<int>(eval("Plus"_("Plus"_(2, 3), 2, 2))) == 9);
    CHECK(get<int>(eval("Plus"_("Plus"_(3, 2), 2, 2))) == 9);
  }

  SECTION("Strings") {
    CHECK(get<string>(eval("StringJoin"_((string) "howdie", (string) " ", (string) "world"))) ==
          "howdie world");
  }

  SECTION("Booleans") {
    CHECK(get<bool>(eval("Greater"_(5, 2))));
    CHECK(!get<bool>(eval("Greater"_(2, 5))));
  }

  SECTION("Symbols") {
    CHECK(get<boss::Symbol>(eval("Symbol"_((string) "x"))).getName() == "x");

    auto expression = get<boss::ComplexExpression>(eval("UndefinedFunction"_(9))); // NOLINT

    CHECK(expression.getHead().getName() == "UndefinedFunction");
    CHECK(get<int>(expression.getArguments()[0]) == 9);

    CHECK(get<std::string>(
              get<boss::ComplexExpression>(eval("UndefinedFunction"_((string) "Hello World!")))
                  .getArguments()[0]) == "Hello World!");
  }
}
#endif // WSINTERFACE
