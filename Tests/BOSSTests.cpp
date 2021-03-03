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
  TestType engine;
  static auto eval = [&engine](boss::Expression const& expression) mutable {
    return engine.evaluate(expression);
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

  SECTION("Relational") {
    eval("CreateTable"_("Customer"_, "FirstName", "LastName"));
    eval("InsertInto"_("Customer"_, "John", "McCarthy"));
    eval("InsertInto"_("Customer"_, "Sam", "Madden"));
    eval("InsertInto"_("Customer"_, "Barbara", "Liskov"));
    SECTION("Selection") {
      auto const& sam = eval(
          "Select"_("Customer"_,
                    "Function"_("tuple"_, "StringContainsQ"_("Madden", "Column"_("tuple"_, 2)))));
      REQUIRE(sam == "List"_("List"_("Sam", "Madden")));
      REQUIRE(sam != "List"_("List"_("Barbara", "Liskov")));
    }

    SECTION("Aggregation") {
      REQUIRE(eval("GroupBy"_("Customer"_, "Function"_(0), "Count"_)) == Value(3));
      REQUIRE(
          eval("GroupBy"_(("Select"_("Customer"_,
                                     "Function"_("tuple"_, "StringContainsQ"_(
                                                               "Madden", "Column"_("tuple"_, 2))))),
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
                                   "Equal"_("Column"_("left"_, 2), "Column"_("right"_, 1)))));
      INFO(get<boss::ComplexExpression>(result));
      REQUIRE(get<boss::ComplexExpression>(result).getArguments().size() == dataSetSize);
    }
  }
}
#endif // WSINTERFACE
