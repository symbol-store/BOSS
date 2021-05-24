#include "Engines/MLIREngine/Executors/Interpreter.hpp"
#include "Utilities.hpp"
#include <catch2/catch.hpp>
#include <iostream>

using boss::utilities::operator""_;

TEST_CASE("InterpreterTest") {

  SECTION("PlusMixedArgs") {
    interpreter::Interpreter i(new new_runtime::Database);

    auto result = i.evaluate("Plus"_(1, "x"_, 2, 3));

    CHECK(std::get<boss::ComplexExpression>(result).getHead().getName() == "Plus");
    CHECK(std::get<boss::Symbol>(std::get<boss::ComplexExpression>(result).getArguments()[0]).getName() == "x");
    CHECK(std::get<int>(std::get<boss::ComplexExpression>(result).getArguments()[1]) == 6);
  }

}
