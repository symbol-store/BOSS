#include <variant>
#define CATCH_CONFIG_MAIN
#include "../Source/BOSS.hpp"
#include "../Source/Utilities.hpp"
#include <catch2/catch.hpp>
#ifdef WSINTERFACE
using std::get;
using std::string;
using boss::utilities::operator""_;

using Value = boss::Expression::ReturnType;

TEMPLATE_TEST_CASE("Simpletons", "", boss::engines::wolfram::Engine) { // NOLINT
  static auto eval = [e = TestType()](boss::Expression const& expression) mutable {
    return e.evaluate(expression);
  };

  SECTION("Basics") {
    REQUIRE(eval("Plus"_(5, 4)) == Value(9));
    REQUIRE(eval("Plus"_(5, 2, 2)) == Value(9));
    REQUIRE(eval("Plus"_(5, 2, 2)) == Value(9));
    REQUIRE(eval("Plus"_("Plus"_(2, 3), 2, 2)) == Value(9));
    REQUIRE(eval("Plus"_("Plus"_(3, 2), 2, 2)) == Value(9));
    REQUIRE(eval("StringJoin"_("howdie", " ", "world")) == Value("howdie world"));
    REQUIRE(get<bool>(eval("Greater"_(5, 2))));
    REQUIRE(!get<bool>(eval("Greater"_(2, 5))));
    REQUIRE(get<boss::Expression::Symbol>(eval("Symbol"_("x"))).getName() == "x");
    REQUIRE(get<boss::Expression>(eval("UndefinedFunction"_(9))).getHead() == "UndefinedFunction");
  }

  SECTION("State") {
    eval("Set"_("thingy"_, 9)); // NOLINT
    REQUIRE(eval("Evaluate"_("thingy"_)) == Value(9));
  }

  SECTION("Relational") {
    eval("CreateTable"_("Customer"_, "FirstName", "LastName"));
    eval("InsertInto"_("Customer"_, "John", "McCarthy"));
    eval("InsertInto"_("Customer"_, "Sam", "Madden"));
    eval("InsertInto"_("Customer"_, "Barbara", "Liskov"));
    SECTION("Selection") {
      auto const& sam = eval(
          "Select"_("Customer"_,
                    "Function"_("tuple"_, "StringContainsQ"_("Madden", "Extract"_("tuple"_, 2)))));
      REQUIRE(sam == "List"_("List"_("Sam", "Madden")));
      REQUIRE(sam != "List"_("List"_("Barbara", "Liskov")));
    }

    SECTION("Aggregation") {
      REQUIRE(eval("GroupBy"_("Customer"_, "List"_(), "Count"_)) == Value(3));
      REQUIRE(eval("GroupBy"_(
                  ("Select"_("Customer"_,
                             "Function"_("tuple"_,
                                         "StringContainsQ"_("Madden", "Extract"_("tuple"_, 2))))),
                  "Function"_(0), "Count"_)) == Value(1));
    }
  }
}
#endif // WSINTERFACE
