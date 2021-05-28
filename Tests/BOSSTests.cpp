#include <variant>
#define CATCH_CONFIG_MAIN
#include "../Source/BOSS.hpp"
#include "../Source/ExpressionUtilities.hpp"
#include <catch2/catch.hpp>
#ifdef WSINTERFACE
using boss::Expression;
using std::get;
using std::string;
using boss::utilities::operator""_;

template <typename Engine> Engine& getEngine();
template <> boss::engines::wolfram::Engine& getEngine<boss::engines::wolfram::Engine>() {
  static boss::engines::wolfram::Engine e;
  return e;
};

TEMPLATE_TEST_CASE("Basics", "[basics]", boss::engines::wolfram::Engine) { // NOLINT
  auto& engine = getEngine<TestType>();
  auto eval = [&engine](boss::Expression const& expression) mutable {
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

  SECTION("Relational (simple)") {
    eval("CreateTable"_("Customer"_, "FirstName"_, "LastName"_));
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
      REQUIRE(eval("Group"_("Customer"_, "Function"_(0), "Count"_)) == "List"_("List"_(3)));
      REQUIRE(
          eval("Group"_(("Select"_("Customer"_,
                                   "Function"_("tuple"_, "StringContainsQ"_(
                                                             "Madden", "Column"_("tuple"_, 2))))),
                        "Function"_(0), "Count"_)) == "List"_("List"_(1)));
    }

    SECTION("Join") {
      eval("CreateTable"_("Adjacency1"_, "From", "To"));
      eval("CreateTable"_("Adjacency2"_, "From2", "To2"));
      auto const dataSetSize = 10;
      for(int i = 0U; i < dataSetSize; i++) {
        eval("InsertInto"_("Adjacency1"_, i, dataSetSize + i));
        eval("InsertInto"_("Adjacency2"_, dataSetSize + i, i));
      }
      auto const& result =
          eval("Join"_("Adjacency1"_, "Adjacency2"_,
                       "Function"_("List"_("tuple"_),
                                   "Equal"_("Column"_("tuple"_, 2), "Column"_("tuple"_, 3)))));
      INFO(get<boss::ComplexExpression>(result));
      REQUIRE(get<boss::ComplexExpression>(result).getArguments().size() == dataSetSize);
    }
  }

  SECTION("Relational (with multiple column types)") {
    eval("CreateTable"_("Customer"_, "ID"_, "FirstName"_, "LastName"_, "BirthYear"_, "Country"_));
    INFO(eval("Length"_("Select"_("Customer"_, "Function"_(true)))));

    REQUIRE(std::get<int>(eval("Length"_("Select"_("Customer"_, "Function"_(true))))) == 0);
    auto const& emptyTable = eval("Select"_("Customer"_, "Function"_(true)));
    CHECK(std::get<int>(eval("Length"_(emptyTable))) == 0);
    eval("InsertInto"_("Customer"_, 1, "John", "McCarthy", 1927, "USA"));  // NOLINT
    eval("InsertInto"_("Customer"_, 2, "Sam", "Madden", 1976, "USA"));     // NOLINT
    eval("InsertInto"_("Customer"_, 3, "Barbara", "Liskov", 1939, "USA")); // NOLINT
    INFO("Select"_("Customer"_, "Function"_(true)));
    CHECK(eval("Length"_("Select"_("Customer"_, "Function"_(true)))) == Expression(3));
    auto const& fullTable = eval("Select"_("Customer"_, "Function"_(true)));
    CHECK(get<int>(eval("Length"_(fullTable))) == 3);
    CHECK(get<std::string>(eval("Extract"_("Extract"_("Select"_("Customer"_, "Function"_(true)), 2),
                                           3))) == "Madden");

    SECTION("Selection") {
      auto const& sam = eval("Select"_(
          "Customer"_,
          "Function"_("List"_("tuple"_), "StringContainsQ"_("Madden", "Column"_("tuple"_, 3)))));
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
      auto const& fullnames = eval(
          "Project"_("Customer"_, "As"_("FirstName"_, "FirstName"_, "LastName"_, "LastName"_)));
      INFO("Project"_("Customer"_, "As"_("FirstName"_, "FirstName"_, "LastName"_, "LastName"_)));
      INFO(fullnames);
      CHECK(get<int>(eval("Length"_(fullnames))) == 3);
      auto const& firstNames = eval("Project"_("Customer"_, "As"_("FirstName"_, "FirstName"_)));
      INFO(eval("Extract"_("Extract"_(fullnames, 1), 1)));
      CHECK(get<string>(eval("Extract"_("Extract"_(firstNames, 1), 1))) ==
            get<string>(eval("Extract"_("Extract"_(fullnames, 1), 1))));
      auto const& lastNames = eval("Project"_("Customer"_, "As"_("LastName"_, "LastName"_)));
      INFO("lastnames=" << eval("Extract"_("Extract"_(lastNames, 1), 1)));
      INFO("fullnames=" << eval("Extract"_("Extract"_(fullnames, 1), 2)));
      CHECK(get<string>(eval("Extract"_("Extract"_(lastNames, 1), 1))) ==
            get<string>(eval("Extract"_("Extract"_(fullnames, 1), 2))));
    }

    SECTION("Sorting") {
      auto const& sortedByLastName = eval("SortBy"_("Select"_("Customer"_, "Function"_(true)),
                                                    "Function"_("tuple"_, "Column"_("tuple"_, 3))));
      auto const& liskovRow = eval("Extract"_(sortedByLastName, 1));
      auto const& MaddenRow = eval("Extract"_(sortedByLastName, 2));
      CHECK(get<string>(eval("Extract"_(liskovRow, 3))) == "Liskov");
      CHECK(get<string>(eval("Extract"_(MaddenRow, 3))) == "Madden");
    }

    SECTION("Aggregation") {
      auto const& countRows = eval("Group"_("Customer"_, "Function"_(0), "Count"_));
      INFO("countRows=" << countRows << "\n" << eval("Extract"_("Extract"_(countRows, 1))));
      CHECK(get<int>(eval("Extract"_("Extract"_(countRows, 1), 1))) == 3);
      CHECK(get<int>(eval("Extract"_(
                "Extract"_("Group"_(("Select"_("Customer"_, "Where"_("StringContainsQ"_(
                                                                "Madden", "LastName"_)))),
                                    "Function"_(0), "Count"_),
                           1),
                1))) == 1);
    }
  }
}

TEMPLATE_TEST_CASE("WolframSpecifics", "[wolfram]", boss::engines::wolfram::Engine) { // NOLINT
  using ExpressionBuilder =
      boss::utilities::ExtensibleExpressionBuilder<boss::engines::wolfram::WolframExpressionSystem>;

  auto& engine = getEngine<TestType>();
  auto eval = [&engine](boss::engines::wolfram::Expression const& expression) mutable {
    return engine.evaluate(expression);
  };

  SECTION("AdditionOfVector") {
    CHECK(get<int>(eval(ExpressionBuilder("Apply")("Plus"_, std::vector<int>{2, 3, 4}))) ==
          9); // NOLINT
  }
}

#endif // WSINTERFACE
