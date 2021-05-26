#include "Engines/MLIREngine/Executors/Interpreter.hpp"
#include "Utilities.hpp"
#include <catch2/catch.hpp>
#include <iostream>

using boss::utilities::operator""_;

TEST_CASE("InterpreterTest") {

  SECTION("PlusMixedArgs") {
    auto* database = new new_runtime::Database;
    interpreter::Interpreter i(database);

    auto result = i.evaluate("Plus"_(1, "x"_, 2, 3));

    CHECK(std::get<boss::ComplexExpression>(result).getHead().getName() == "Plus");
    CHECK(std::get<boss::Symbol>(std::get<boss::ComplexExpression>(result).getArguments()[0]).getName() == "x");
    CHECK(std::get<int>(std::get<boss::ComplexExpression>(result).getArguments()[1]) == 6);
    delete database;
  }

  SECTION("AssumingDefinition") {
    auto* database = new new_runtime::Database;
    interpreter::Interpreter i(database);

    auto result = i.evaluate("Assuming"_("x"_, 5, "Plus"_("x"_, 2)));

    CHECK(std::get<int>(result) == 7);
    delete database;
  }

}
