#include "../Source/ExpressionParser.hpp"

extern "C" {
#include <chibi/eval.h>
#include <chibi/sexp.h>
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ios>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <vector>

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

  SECTION("translates \\uXXXX escapes into UTF-8 strings") {
    auto const result = boss::evaluate_expression(ctx, env, "\"caf\\u00e9\"");
    CHECK_FALSE(result.is_error);
    CHECK(result.text == "\"caf\xC3\xA9\"\n");
  }

  SECTION("translates a surrogate pair into one UTF-8 character") {
    auto const result = boss::evaluate_expression(ctx, env, "\"\\uD83D\\uDE00\"");
    CHECK_FALSE(result.is_error);
    CHECK(result.text == "\"\xF0\x9F\x98\x80\"\n");
  }

  SECTION("replaces an unpaired surrogate with U+FFFD") {
    auto const result = boss::evaluate_expression(ctx, env, "\"\\uD83D\"");
    CHECK_FALSE(result.is_error);
    CHECK(result.text == "\"\xEF\xBF\xBD\"\n");
  }

  SECTION("rejects U+0000 instead of truncating the string") {
    auto const result = boss::evaluate_expression(ctx, env, "\"a\\u0000b\"");
    CHECK(result.is_error);
    CHECK_THAT(result.text, Catch::Matchers::ContainsSubstring("U+0000"));
  }
}

namespace {
// Fails every write, so an ostream using it with badbit exceptions enabled throws.
class FailingStreamBuffer : public std::streambuf {
protected:
  int_type overflow(int_type /*unused*/) override { return traits_type::eof(); }
};
} // namespace

TEST_CASE("pretty_print_expression", "[ExpressionParser][pretty]") {
  using boss::utilities::operator""_;
  boss::ComplexExpression const expression = "Plus"_("x"_, "y"_);
  std::stringstream compactOut;
  compactOut << expression;

  SECTION("renders through the chibi printer") {
    std::stringstream out;
    CHECK(boss::pretty_print_expression(out, expression));
    CHECK(out.str() == "(Plus x y)\n");
  }

  SECTION("renders the values held in span arguments") {
    boss::ComplexExpression const spanBacked =
        "Plus"_(boss::Span<std::int64_t>(std::vector<std::int64_t> {1, 2}));
    std::stringstream out;
    CHECK(boss::pretty_print_expression(out, spanBacked));
    CHECK(out.str() == "(Plus 1 2)\n");
  }

  SECTION("is the hook that boss::pretty uses once BossPrettyPrint.cpp is linked") {
    CHECK(boss::prettyPrintHook() == &boss::pretty_print_expression);
    std::stringstream out;
    out << boss::pretty << expression;
    CHECK(out.str() == "(Plus x y)\n");
    CHECK(out.str() != compactOut.str());
  }

  SECTION("lets a stream's exception through and keeps rendering afterwards") {
    FailingStreamBuffer failingBuffer;
    std::ostream failingStream(&failingBuffer);
    failingStream.exceptions(std::ios::badbit);
    CHECK_THROWS_AS(boss::pretty_print_expression(failingStream, expression),
                    std::ios_base::failure);
    std::stringstream out;
    CHECK(boss::pretty_print_expression(out, expression));
    CHECK(out.str() == "(Plus x y)\n");
  }
}
