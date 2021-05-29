#include "Engines/MLIREngine/Executors/Compiler.hpp"
#include "Utilities.hpp"
#include <catch2/catch.hpp>
#include <iostream>

using boss::utilities::operator""_;
using std::string;

TEST_CASE("CompilerTest") {

  SECTION("StringJoin") {
    boss::engines::mlir::compiler::Compiler compiler(nullptr);

    CHECK(std::get<string>(compiler.evaluate("StringJoin"_((string) "howdie", (string) " ", (string) "world"))) ==
          "howdie world");
  }

  // TODO check unevaluated symbols

}
