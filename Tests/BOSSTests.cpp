#include <variant>
#define CATCH_CONFIG_MAIN
#include "../Source/BOSS.hpp"
#include "../Source/Utilities.hpp"
#include <catch2/catch.hpp>
#ifdef WSINTERFACE
using std::get;
using std::string;
using boss::Expression;
using boss::utilities::operator""_;

TEMPLATE_TEST_CASE("Basics", "[basics]", boss::engines::wolfram::Engine) { // NOLINT
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
      REQUIRE(eval("GroupBy"_("Customer"_, "Function"_(0), "Count"_)) == Expression(3));
      REQUIRE(eval("GroupBy"_(
                  ("Select"_("Customer"_,
                             "Function"_("tuple"_,
                                         "StringContainsQ"_("Madden", "Column"_("tuple"_, 2))))),
                  "Function"_(0), "Count"_)) == Expression(1));
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

TEMPLATE_TEST_CASE("Relational", "[relational]", boss::engines::wolfram::Engine) { // NOLINT
  static auto eval = [e = TestType()](boss::Expression const& expression) mutable {
    return e.evaluate(expression);
  };
  SECTION("TableCreation") {
    eval("CreateTable"_("Customer"_));
    eval("AddColumn"_("Customer"_, "ID", "FirstName", "LastName", "BirthYear", "Country"));
    CHECK(std::get<int>(eval("Length"_("Customer"_))) == 0);
    auto const& emptyTable = eval("Customer"_);
    CHECK(std::get<int>(eval("Length"_(emptyTable))) == 0);
    eval("InsertInto"_("Customer"_, "List"_(1, "John", "McCarthy", 1927, "USA")));  // NOLINT
    eval("InsertInto"_("Customer"_, "List"_(2, "Sam", "Madden", 1976, "USA")));     // NOLINT
    eval("InsertInto"_("Customer"_, "List"_(3, "Barbara", "Liskov", 1939, "USA"))); // NOLINT
    CHECK(get<int>(eval("Length"_("Customer"_))) == 3);
    auto const& fullTable = eval("Customer"_);
    CHECK(get<int>(eval("Length"_(fullTable))) == 3);
    CHECK(get<std::string>(eval("Extract"_("Extract"_("Customer"_, 2), 3))) == "Madden");
  }

  SECTION("Selection") {
    auto const& sam = eval(
        "Select"_("Customer"_, "Function"_("List"_("tuple"_),
                                           "StringContainsQ"_("Madden", "Column"_("tuple"_, 3)))));
    CHECK(get<int>(eval("Length"_(sam))) == 1);
    auto const& samRow = eval("Extract"_(sam, 1));
    CHECK(get<int>(eval("Length"_(samRow))) == 5);
    CHECK(get<string>(eval("Extract"_(samRow, 2))) == "Sam");
    CHECK(get<string>(eval("Extract"_(samRow, 3))) == "Madden");
    auto const& none = eval("Select"_("Customer"_, "Function"_(false)));
    CHECK(get<int>(eval("Length"_(none))) == 0);
    auto const& all = eval("Select"_("Customer"_, "Function"_(true)));
    CHECK(get<int>(eval("Length"_(all))) == 3);
    auto const& johnRow = eval("Extract"_(all, 1));
    auto const& barbaraRow = eval("Extract"_(all, 3));
    CHECK(get<string>(eval("Extract"_(johnRow, 2))) == "John");
    CHECK(get<string>(eval("Extract"_(barbaraRow, 2))) == "Barbara");
  }

  SECTION("Projection") {
    auto const& empty = eval("Project"_("Customer"_, "List"_()));
    CHECK(get<int>(eval("Length"_(empty))) == 0);
    auto const& fullnames = eval("Project"_("Customer"_, "List"_("FirstName", "LastName")));
    CHECK(get<int>(eval("Length"_(fullnames))) == 3);
    auto const& firstNames = eval("Project"_("Customer"_, "List"_("FirstName")));
    CHECK(get<string>(eval("Extract"_("Extract"_(firstNames, 1), 1))) ==
          get<string>(eval("Extract"_("Extract"_(fullnames, 1), 1))));
    auto const& lastNames = eval("Project"_("Customer"_, "List"_("LastName")));
    CHECK(get<string>(eval("Extract"_("Extract"_(lastNames, 1), 1))) ==
          get<string>(eval("Extract"_("Extract"_(fullnames, 1), 2))));
  }

  SECTION("Sorting") {
    auto const& sortedByLastName =
        eval("SortBy"_("Customer"_, "Function"_("List"_("tuple"_), "Column"_("tuple"_, 3))));
    auto const& liskovRow = eval("Extract"_(sortedByLastName, 1));
    auto const& MaddenRow = eval("Extract"_(sortedByLastName, 2));
    CHECK(get<string>(eval("Extract"_(liskovRow, 3))) == "Liskov");
    CHECK(get<string>(eval("Extract"_(MaddenRow, 3))) == "Madden");
  }

  SECTION("Aggregation") {
    auto const& countRows = eval("GroupBy"_("Customer"_, "Function"_(0),
                                            "Function"_("List"_("tuple"_), "Count"_("tuple"_))));
    CHECK(get<int>(eval("Extract"_(countRows, 1))) == 3);
    CHECK(
        get<int>(eval("Extract"_(
            "GroupBy"_(("Select"_("Customer"_, "Function"_("List"_("tuple"_),
                                                           "StringContainsQ"_(
                                                               "Madden", "Column"_("tuple"_, 3))))),
                       "Function"_(0), "Function"_("List"_("tuple"_), "Count"_("tuple"_))),
            1))) == 1);
  }
}

#endif // WSINTERFACE
