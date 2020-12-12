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

  SECTION("Basics") {
    REQUIRE(get<int>(eval("Plus"_(5, 4))) == 9);
    REQUIRE(get<int>(eval("Plus"_(5, 2, 2))) == 9);
    REQUIRE(get<int>(eval("Plus"_(5, 2, 2))) == 9);
    REQUIRE(get<int>(eval("Plus"_("Plus"_(2, 3), 2, 2))) == 9);
    REQUIRE(get<int>(eval("Plus"_("Plus"_(3, 2), 2, 2))) == 9);
    REQUIRE(get<string>(eval("StringJoin"_("howdie", " ", "world"))) == "howdie world");
    REQUIRE(get<bool>(eval("Greater"_(5, 2))));
    REQUIRE(!get<bool>(eval("Greater"_(2, 5))));
    REQUIRE(get<boss::Expression::Symbol>(eval("Symbol"_("x"))).getName() == "x");
    REQUIRE(get<boss::Expression>(eval("UndefinedFunction"_(9))).getHead() == "UndefinedFunction");
  }

  SECTION("State") {
    eval("Set"_("thingy"_, 9));
    REQUIRE(get<int>(eval("Evaluate"_("thingy"_))) == 9);
  }

  SECTION("Relational") {
    eval("CreateTable"_("Customer"_, "FirstName", "LastName"));
    eval("InsertInto"_("Customer"_, "List"_("John", "McCarthy")));
    eval("InsertInto"_("Customer"_, "List"_("Sam", "Madden")));
    eval("InsertInto"_("Customer"_, "List"_("Barbara", "Liskov")));
    REQUIRE(get<int>(eval("GroupBy2"_("Customer"_, "List"_(), "Count"_))) == 3);
    REQUIRE(
        get<int>(eval("GroupBy2"_(
            ("Select"_("Customer"_, "Function"_("tuple"_, "StringContainsQ"_(
                                                              "Madden", "Extract"_("tuple"_, 2))))),
            "List"_(), "Count"_))) == 1);
  }
}
#endif // WSINTERFACE
