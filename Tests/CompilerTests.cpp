#include "Engines/MLIREngine/Executors/Compiler.hpp"
#include "Utilities.hpp"
#include <catch2/catch.hpp>
#include <iostream>

using boss::utilities::operator""_;
using std::string;

TEST_CASE("CompilerTest") {

  SECTION("StringJoin") {
    boss::engines::mlir::compiler::Compiler compiler(nullptr);

    CHECK(std::get<string>(compiler.evaluate(
              "StringJoin"_((string) "howdie", (string) " ", (string) "world"))) == "howdie world");
  }

  SECTION("Symbols") {
    boss::engines::mlir::compiler::Compiler compiler(nullptr);

    CHECK(std::get<boss::Symbol>(compiler.evaluate("Symbol"_((string) "x"))).getName() == "x");

    auto expression = std::get<boss::ComplexExpression>(compiler.evaluate("UndefinedFunction"_(9)));

    CHECK(expression.getHead().getName() == "UndefinedFunction");
    CHECK(std::get<int>(expression.getArguments()[0]) == 9);

    CHECK(
        std::get<std::string>(std::get<boss::ComplexExpression>(
                                  compiler.evaluate("UndefinedFunction"_((string) "Hello World!")))
                                  .getArguments()[0]) == "Hello World!");
  }

  SECTION("StringEquality") {
    boss::engines::mlir::compiler::Compiler compiler(nullptr);

    CHECK(!std::get<bool>(compiler.evaluate("Eq"_((string)"foo", (string)"bar"))));
    CHECK(std::get<bool>(compiler.evaluate("Eq"_((string)"world", (string)"world"))));

  }

  // TODO check unevaluated symbols
}
