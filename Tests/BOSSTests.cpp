#define CATCH_CONFIG_RUNNER
#include "../Source/BOSS.hpp"
#include "../Source/BootstrapEngine.hpp"
#include "../Source/ExpressionUtilities.hpp"
#include <arrow/array.h>
#include <catch2/catch.hpp>
#include <variant>
using boss::Expression;
using boss::get;
using std::string;
using boss::utilities::operator""_;
using Catch::Generators::random;
using Catch::Generators::take;
using std::vector;
using namespace Catch::Matchers;

static std::vector<string>
    librariesToTest{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

TEST_CASE("Basics", "[basics]") { // NOLINT
  auto engine = boss::BootstrapEngine();
  auto eval = [&engine](boss::Expression&& expression) mutable {
    return engine.evaluate(
        "EvaluateInEngine"_(GENERATE(from_range(librariesToTest)), move(expression)));
  };
  CHECK_THROWS_MATCHES(
      engine.evaluate("EvaluateInEngine"_(9, 5)), boss::bad_variant_access,
      Message("expected and actual type mismatch in expression \"9\", expected string"));

  SECTION("Atomics") {
    CHECK(get<int>(eval(9)) == 9); // NOLINT
  }

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

  SECTION("Floats") {
    auto const twoAndAHalf = 2.5F;
    auto const two = 2.0F;
    auto const quantum = 0.001F;
    CHECK(std::fabs(get<float>(eval("Plus"_(twoAndAHalf, twoAndAHalf))) - two * twoAndAHalf) <
          quantum);
  }

  SECTION("Booleans") {
    CHECK(get<bool>(eval("Greater"_(5, 2))));
    CHECK(!get<bool>(eval("Greater"_(2, 5))));
  }

  SECTION("Symbols") {
    CHECK(get<boss::Symbol>(eval("Symbol"_((string) "x"))).getName() == "x");
    auto expression = get<boss::ComplexExpression>(
        eval("UndefinedFunction"_(9))); // NOLINT(readability-magic-numbers)

    CHECK(expression.getHead().getName() == "UndefinedFunction");
    CHECK(get<int>(expression.getArguments().at(0)) == 9);

    CHECK(get<std::string>(
              get<boss::ComplexExpression>(eval("UndefinedFunction"_((string) "Hello World!")))
                  .getArguments()[0]) == "Hello World!");
  }

  SECTION("Interpolation") {
    auto thing = GENERATE(
        take(1, chunk(3, filter([](int i) { return i % 2 == 1; }, random(1, 1000))))); // NOLINT
    std::sort(begin(thing), end(thing));
    auto y = GENERATE(
        take(1, chunk(3, filter([](int i) { return i % 2 == 1; }, random(1, 1000))))); // NOLINT

    eval("CreateTable"_("InterpolationTable"_, "x"_, "y"_));
    eval("InsertInto"_("InterpolationTable"_, thing[0], y[0]));
    eval("InsertInto"_("InterpolationTable"_, thing[1], "Interpolate"_("x"_)));
    eval("InsertInto"_("InterpolationTable"_, thing[2], y[2]));
    REQUIRE(eval("Project"_("InterpolationTable"_, "As"_("y"_, "y"_))) ==
            "List"_("List"_(y[0]), "List"_((y[0] + y[2]) / 2), "List"_(y[2])));
    REQUIRE(eval("Project"_("InterpolationTable"_, "As"_("x"_, "x"_))) ==
            "List"_("List"_(thing[0]), "List"_(thing[1]), "List"_(thing[2])));
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
      REQUIRE(eval("Group"_("Customer"_, "Function"_(0), "Count"_)) == "List"_("List"_(3, 0)));
      REQUIRE(eval("Group"_("Customer"_, "Count"_)) == "List"_("List"_(3)));
      REQUIRE(
          eval("Group"_(("Select"_("Customer"_,
                                   "Function"_("tuple"_, "StringContainsQ"_(
                                                             "Madden", "Column"_("tuple"_, 2))))),
                        "Function"_(0), "Count"_)) == "List"_("List"_(1, 0)));
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

  SECTION("Inserting") {
    eval("CreateTable"_("InsertTable"_, "duh"_));
    eval("InsertInto"_("InsertTable"_, "Plus"_(1, 2)));
    REQUIRE(eval("Select"_("InsertTable"_, "Function"_(true))) == "List"_("List"_(3)));
  }

  SECTION("Relational (with multiple column types)") {
    eval("CreateTable"_("Customer"_, "ID"_, "FirstName"_, "LastName"_, "BirthYear"_, "Country"_));
    INFO(eval("Length"_("Select"_("Customer"_, "Function"_(true)))));

    REQUIRE(get<int>(eval("Length"_("Select"_("Customer"_, "Function"_(true))))) == 0);
    auto const& emptyTable = eval("Select"_("Customer"_, "Function"_(true)));
    CHECK(get<int>(eval("Length"_(emptyTable))) == 0);
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

TEST_CASE("Arrays", "[arrays]") { // NOLINT
  auto engine = boss::BootstrapEngine();
  namespace nasty = boss::utilities::nasty;
  auto eval = [&engine](auto&& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(GENERATE(from_range(librariesToTest)), expression));
  };

  std::vector<int32_t> ints{10, 20, 30, 40, 50}; // NOLINT
  std::shared_ptr<arrow::Array> arrayPtr(
      new arrow::Int32Array((long long)ints.size(), arrow::Buffer::Wrap(ints)));

  auto arrayPtrExpr = nasty::arrowArrayToExpression(arrayPtr);
  eval("CreateTable"_("Thingy"_, "Value"_));
  eval("AttachColumns"_("Thingy"_, arrayPtrExpr));

  SECTION("ArrowArrays") {
    CHECK(get<int>(eval("Extract"_(arrayPtrExpr, 1))) == 10);
    CHECK(get<int>(eval("Extract"_(arrayPtrExpr, 2))) == 20);
    CHECK(get<int>(eval("Extract"_(arrayPtrExpr, 3))) == 30);
    CHECK(get<int>(eval("Extract"_(arrayPtrExpr, 4))) == 40);
    CHECK(get<int>(eval("Extract"_(arrayPtrExpr, 5))) == 50);
    CHECK(eval(arrayPtrExpr) == "List"_(10, 20, 30, 40, 50));
  }

  auto compareColumn = [&eval](boss::Expression const& expression, auto const& results) {
    for(auto i = 0; i < results.size(); i++) {
      CHECK(get<typename std::remove_reference_t<decltype(results)>::value_type>(
                eval("Extract"_("Extract"_(expression, i + 1), 1))) == results[i]);
    }
  };

  SECTION("Plus") {
    compareColumn("Project"_("Thingy"_, "As"_("Result"_, "Plus"_("Value"_, "Value"_))),
                  vector<int>{20, 40, 60, 80, 100}); // NOLINT(readability-magic-numbers)
    compareColumn("Project"_("Thingy"_, "As"_("Result"_, "Plus"_("Value"_, 1))),
                  vector<int>{11, 21, 31, 41, 51}); // NOLINT(readability-magic-numbers)
  }

  SECTION("Greater") {
    compareColumn(
        "Project"_("Thingy"_,
                   "As"_("Result"_, "Greater"_("Value"_, 25))), // NOLINT(readability-magic-numbers)
        vector<bool>{false, false, true, true, true});
    compareColumn(
        "Project"_("Thingy"_,
                   "As"_("Result"_, "Greater"_(45, "Value"_))), // NOLINT(readability-magic-numbers)
        vector<bool>{true, true, true, true, false});
  }

  SECTION("Logic") {
    compareColumn(
        "Project"_(
            "Thingy"_,
            "As"_("Result"_, "And"_("Greater"_("Value"_, 25), // NOLINT(readability-magic-numbers)
                                    "Greater"_(45, "Value"_)  // NOLINT(readability-magic-numbers)
                                    ))),
        vector<bool>{false, false, true, true, false});
  }
}

int main(int argc, char* argv[]) {
  Catch::Session session;
  session.cli(session.cli() | Catch::clara::Opt(librariesToTest, "library")["--library"]);
  int returnCode = session.applyCommandLine(argc, argv);
  if(returnCode != 0) {
    return returnCode;
  }
  return session.run();
}
