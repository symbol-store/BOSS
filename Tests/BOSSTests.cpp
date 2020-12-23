#include <variant>
#define CATCH_CONFIG_MAIN
#include "../Source/BOSS.hpp"
#include "../Source/Utilities.hpp"
#include <catch2/catch.hpp>
#ifdef WSINTERFACE
using std::get;
using std::string;
using boss::utilities::operator""_;

using Value = boss::Expression;

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
    REQUIRE(get<boss::Symbol>(eval("Symbol"_("x"))).getName() == "x");
    REQUIRE(get<boss::ComplexExpression>(eval("UndefinedFunction"_(9))).getHead().getName() ==
            "UndefinedFunction");
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

    SECTION("Join") {
      eval("CreateTable"_("Adjacency1"_, "From", "To"));
      eval("CreateTable"_("Adjacency2"_, "From", "To"));
      auto const dataSetSize = 10;
      for(int i = 0U; i < dataSetSize; i++) {
        eval("InsertInto"_("Adjacency1"_, i, dataSetSize + i));
        eval("InsertInto"_("Adjacency2"_, dataSetSize + i, i));
      }
      auto const& result =
          eval("Join"_("Adjacency1"_, "Adjacency2"_,
                       "Function"_("List"_("left"_, "right"_),
                                   "Equal"_("Extract"_("left"_, 2), "Extract"_("right"_, 1)))));
      REQUIRE(get<boss::ComplexExpression>(result).getArguments().size() == dataSetSize);
    }
  }
}
#endif // WSINTERFACE
