#include "../Source/ExpressionParser.hpp"

extern "C" {
#include <chibi/eval.h>
#include <chibi/sexp.h>
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// Catch2 reruns the test case from the top for every SECTION, so each section gets a fresh
// context and a redefinition of boss-print cannot leak into the next one.
TEST_CASE("evaluate_expression", "[ExpressionParser]") {
  sexp ctx = boss::initialize_boss_context();
  REQUIRE(ctx != nullptr);
  boss::BossContextGuard const contextGuard {ctx};
  sexp env = sexp_context_env(ctx);

  SECTION("pretty-prints a complex expression through boss-print") {
    auto const result = boss::evaluate_expression(ctx, env, "(Plus 1 2)");
    CHECK_FALSE(result.is_error);
    CHECK(result.text == "(Plus 1 2)\n");
  }

  SECTION("pretty-prints scalars through boss-print") {
    CHECK(boss::evaluate_expression(ctx, env, "42").text == "42\n");
    CHECK(boss::evaluate_expression(ctx, env, "\"hi\"").text == "\"hi\"\n");
  }

  SECTION("writes with chibi's writer when pretty printing is off") {
    auto const result = boss::evaluate_expression(ctx, env, "(Plus 1 2)", false);
    CHECK_FALSE(result.is_error);
    CHECK(result.text == "(Plus 1 2)");
  }

  SECTION("falls back to chibi's writer when boss-print is not a procedure") {
    sexp_env_define(ctx, env, sexp_intern(ctx, "boss-print", -1), SEXP_FALSE);
    auto const result = boss::evaluate_expression(ctx, env, "(Plus 1 2)");
    CHECK_FALSE(result.is_error);
    CHECK(result.text == "(Plus 1 2)");
  }

  SECTION("reports a read error with chibi's exception text") {
    auto const result = boss::evaluate_expression(ctx, env, "(Plus 1");
    CHECK(result.is_error);
    CHECK_THAT(result.text, Catch::Matchers::ContainsSubstring("missing trailing"));
  }
}
