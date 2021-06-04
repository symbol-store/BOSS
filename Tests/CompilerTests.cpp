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
              "StringJoin"_((string) "howdie", (string) " ", (string) "world"), false)) == "howdie world");
  }

  SECTION("Symbols") {
    boss::engines::mlir::compiler::Compiler compiler(nullptr);

    CHECK(std::get<boss::Symbol>(compiler.evaluate("Symbol"_((string) "x"), false)).getName() == "x");

    auto expression = std::get<boss::ComplexExpression>(compiler.evaluate("UndefinedFunction"_(9), false));

    CHECK(expression.getHead().getName() == "UndefinedFunction");
    CHECK(std::get<int>(expression.getArguments()[0]) == 9);

    CHECK(
        std::get<std::string>(std::get<boss::ComplexExpression>(
                  compiler.evaluate("UndefinedFunction"_((string) "Hello World!"), false))
                                  .getArguments()[0]) == "Hello World!");
  }

  SECTION("StringEquality") {
    boss::engines::mlir::compiler::Compiler compiler(nullptr);

    CHECK(!std::get<bool>(compiler.evaluate("Eq"_((string) "foo", (string) "bar"), false)));
    CHECK(std::get<bool>(compiler.evaluate("Eq"_((string) "world", (string) "world"), false)));

  }

  // TODO check unevaluated symbols
}
