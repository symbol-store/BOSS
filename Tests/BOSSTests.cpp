#include "../Source/BOSS.hpp"
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#ifdef WSINTERFACE
TEMPLATE_TEST_CASE("Simpletons", "", boss::engines::wolfram::Engine) { // NOLINT
  using std::get;
  using std::string;
  using boss::Expression;
  using Symbol = Expression::Symbol;
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
    REQUIRE(get<Symbol>(engine.evaluate({"Symbol", {"x"}})).getName() == "x");
    REQUIRE(get<Expression>(engine.evaluate({"UndefinedFunction", {9}})).getHead() ==
            "UndefinedFunction");
  }

  SECTION("State") {
    engine.evaluate({"Set", {Symbol("thingy"), 9}});
    REQUIRE(get<int>(engine.evaluate({"Evaluate", {Symbol("thingy")}})) == 9);
  }

  SECTION("Relational") {
    engine.evaluate({"Create", {"Table", Symbol("Customer"), "FirstName", "LastName"}});
    engine.evaluate({"InsertInto", {Symbol("Customer"), Expression{"List", {"John", "McCarthy"}}}});
    engine.evaluate({"InsertInto", {Symbol("Customer"), Expression{"List", {"Sam", "Madden"}}}});
    REQUIRE(get<int>(engine.evaluate(
                {"Project", {Expression{"Count", {"Name"}}, "From", Symbol("Customer")}})) == 2);
    engine.evaluate(
        {"InsertInto", {Symbol("Customer"), Expression{"List", {"Barbara", "Liskov"}}}});
    REQUIRE(get<int>(engine.evaluate(
                {"Project", {Expression{"Count", {"Name"}}, "From", Symbol("Customer")}})) == 3);
    REQUIRE(
        get<int>(engine.evaluate(
            {"Length",
             {Expression{
                 "Select",
                 {Symbol("Customer"),
                  Expression{"Function",
                             {Symbol("tuple"),
                              Expression{"StringContainsQ",
                                         {"Madden",
                                          Expression{"Extract", {Symbol("tuple"), 2}}}}}}}}}})) ==
        1);
  }
}
#endif // WSINTERFACE
