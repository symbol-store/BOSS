#define CATCH_CONFIG_RUNNER
#include "../Source/BOSS.hpp"
#include "../Source/BootstrapEngine.hpp"
#include "../Source/ExpressionUtilities.hpp"
#include <arrow/array.h>
#include <arrow/builder.h>
#include <catch2/catch.hpp>
#include <numeric>
#include <variant>
using boss::Expression;
using std::string;
using std::literals::string_literals::operator""s;
using boss::utilities::operator""_;
using Catch::Generators::random;
using Catch::Generators::take;
using Catch::Generators::values;
using std::vector;
using namespace Catch::Matchers;
using boss::expressions::CloneReason;
using boss::expressions::generic::get;
using boss::expressions::generic::get_if;
using boss::expressions::generic::holds_alternative;
namespace boss {
using boss::expressions::atoms::Span;
};
using std::int64_t;

static std::vector<string>
    librariesToTest{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

TEST_CASE("Subspans work correctly", "[spans]") {
  auto input = boss::Span<int64_t>{std::vector<int64_t>{1, 2, 4, 3}};
  auto subrange = std::move(input).subspan(1, 3);
  CHECK(subrange.size() == 3);
  CHECK(subrange[0] == 2);
  CHECK(subrange[1] == 4);
  CHECK(subrange[2] == 3);
  auto subrange2 = boss::Span<int64_t>{std::vector<int64_t>{1, 2, 3, 2}}.subspan(2);
  CHECK(subrange2[0] == 3);
  CHECK(subrange2[1] == 2);
}

TEST_CASE("Expressions", "[expressions]") {
  auto const v1 = GENERATE(take(3, random<std::int64_t>(1, 100)));
  auto const v2 = GENERATE(take(3, random<std::int64_t>(1, 100)));
  auto const e = "UnevaluatedPlus"_(v1, v2);
  CHECK(e.getHead().getName() == "UnevaluatedPlus");
  CHECK(e.getArguments().at(0) == v1);
  CHECK(e.getArguments().at(1) == v2);

  SECTION("static expression arguments") {
    auto staticArgumentExpression =
        boss::expressions::ComplexExpressionWithStaticArguments<std::int64_t, std::int64_t>(
            "UnevaluatedPlus"_, {v1, v2});
    CHECK(e == staticArgumentExpression);
  }

  SECTION("span expression arguments") {
    std::array<int64_t, 2> values = {v1, v2};
    auto spanArgumentExpression = boss::expressions::ComplexExpression(
        "UnevaluatedPlus"_, {}, {},
        {boss::expressions::atoms::Span<int64_t>(&values[0], 2, [](auto&& /*unused*/) {})});
    CHECK(e == spanArgumentExpression);
  }

  SECTION("nested span expression arguments") {
    std::array<int64_t, 2> values = {v1, v2};
    auto nested = boss::expressions::ComplexExpression(
        "UnevaluatedPlus"_, {}, {},
        {boss::expressions::atoms::Span<int64_t const>(&values[0], 2, [](auto&& /*unused*/) {})});
    boss::expressions::ExpressionArguments subExpressions;
    subExpressions.push_back(std::move(nested));
    auto spanArgumentExpression =
        boss::expressions::ComplexExpression("UnevaluatedPlus"_, {}, std::move(subExpressions), {});
    CHECK("UnevaluatedPlus"_("UnevaluatedPlus"_(v1, v2)) == spanArgumentExpression);
  }
}

TEST_CASE("Expressions with static Arguments", "[expressions]") {
  SECTION("Atomic type subexpressions") {
    auto v1 = GENERATE(take(3, random<std::int64_t>(1, 100)));
    auto v2 = GENERATE(take(3, random<std::int64_t>(1, 100)));
    auto const e = boss::ComplexExpressionWithStaticArguments<std::int64_t, std::int64_t>(
        "UnevaluatedPlus"_, {v1, v2}, {}, {});
    CHECK(e.getHead().getName() == "UnevaluatedPlus");
    CHECK(e.getArguments().at(0) == v1);
    CHECK(e.getArguments().at(1) == v2);
  }
  SECTION("Complex subexpressions") {
    auto v1 = GENERATE(take(3, random<std::int64_t>(1, 100)));
    auto const e = boss::ComplexExpressionWithStaticArguments<
        boss::ComplexExpressionWithStaticArguments<std::int64_t>>(
        {"Duh"_,
         boss::ComplexExpressionWithStaticArguments<std::int64_t>{"UnevaluatedPlus"_, {v1}, {}, {}},
         {},
         {}});
    CHECK(e.getHead().getName() == "Duh");
    // TODO: this check should be enabled but requires a way to construct argument wrappers
    // from statically typed expressions
    // std::visit(
    //     [](auto&& arg) {
    //       CHECK(std::is_same_v<decltype(arg), boss::expressions::ComplexExpression>);
    //     },
    //     e.getArguments().at(0).getArgument());
  }
}

TEST_CASE("Expression Transformation", "[expressions]") {
  auto v1 = GENERATE(take(3, random<std::int64_t>(1, 100)));
  auto v2 = GENERATE(take(3, random<std::int64_t>(1, 100)));
  auto e = "UnevaluatedPlus"_(v1, v2);
  REQUIRE(*std::begin(e.getArguments()) == v1);
  get<std::int64_t>(*std::begin(e.getArguments()))++;
  REQUIRE(*std::begin(e.getArguments()) == v1 + 1);
  std::transform(std::make_move_iterator(std::begin(e.getArguments())),
                 std::make_move_iterator(std::end(e.getArguments())), e.getArguments().begin(),
                 [](auto&& e) { return get<std::int64_t>(e) + 1; });

  CHECK(e.getArguments().at(0) == v1 + 2);
  CHECK(e.getArguments().at(1) == v2 + 1);
}

TEST_CASE("Expression without arguments", "[expressions]") {
  auto const& e = "UnevaluatedPlus"_();
  CHECK(e.getHead().getName() == "UnevaluatedPlus");
}

class DummyAtom {
public:
  friend std::ostream& operator<<(std::ostream& s, DummyAtom const& /*unused*/) {
    return s << "dummy";
  }
};

TEST_CASE("Expression cast to more general expression system", "[expressions]") {
  auto a = boss::ExtensibleExpressionSystem<>::Expression("howdie"_());
  auto b = (boss::ExtensibleExpressionSystem<DummyAtom>::Expression)std::move(a);
  CHECK(
      get<boss::ExtensibleExpressionSystem<DummyAtom>::ComplexExpression>(b).getHead().getName() ==
      "howdie");
  auto& srcExpr = get<boss::ExtensibleExpressionSystem<DummyAtom>::ComplexExpression>(b);
  auto const& cexpr = std::decay_t<decltype(srcExpr)>(std::move(srcExpr));
  auto const& args = cexpr.getArguments();
  CHECK(args.empty());
}

TEST_CASE("Complex expression's argument cast to more general expression system", "[expressions]") {
  auto a = "List"_("howdie"_(1, 2, 3));
  auto const& b1 =
      (boss::ExtensibleExpressionSystem<DummyAtom>::Expression)(std::move(a).getArgument(0));
  CHECK(
      get<boss::ExtensibleExpressionSystem<DummyAtom>::ComplexExpression>(b1).getHead().getName() ==
      "howdie");
  auto b2 = get<boss::ExtensibleExpressionSystem<DummyAtom>::ComplexExpression>(b1).cloneArgument(
      1, CloneReason::FOR_TESTING);
  CHECK(get<int64_t>(b2) == 2);
}

TEST_CASE("Extract typed arguments from complex expression (using std::accumulate)",
          "[expressions]") {
  auto exprBase = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  auto const& expr0 =
      boss::ExtensibleExpressionSystem<DummyAtom>::ComplexExpression(std::move(exprBase));
  auto str = [](auto const& expr) {
    auto const& args = expr.getArguments();
    return std::accumulate(
        args.begin(), args.end(), expr.getHead().getName(),
        [](auto const& accStr, auto const& arg) {
          return accStr + "_" +
                 visit(boss::utilities::overload(
                           [](auto const& value) { return std::to_string(value); },
                           [](DummyAtom const& /*value*/) { return ""s; },
                           [](boss::ExtensibleExpressionSystem<DummyAtom>::ComplexExpression const&
                                  expr) { return expr.getHead().getName(); },
                           [](boss::Symbol const& symbol) { return symbol.getName(); },
                           [](std::string const& str) { return str; }),
                       arg);
        });
  }(expr0);
  CHECK(str == "List_howdie_1_unknown_hello world");
}

TEST_CASE("Extract typed arguments from complex expression (manual iteration)", "[expressions]") {
  auto exprBase = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  auto const& expr0 =
      boss::ExtensibleExpressionSystem<DummyAtom>::ComplexExpression(std::move(exprBase));
  auto str = [](auto const& expr) {
    auto const& args = expr.getArguments();
    auto size = args.size();
    auto accStr = expr.getHead().getName();
    for(int idx = 0; idx < size; ++idx) {
      accStr +=
          "_" +
          visit(boss::utilities::overload(
                    [](auto const& value) { return std::to_string(value); },
                    [](DummyAtom const& /*value*/) { return ""s; },
                    [](boss::ExtensibleExpressionSystem<DummyAtom>::ComplexExpression const& expr) {
                      return expr.getHead().getName();
                    },
                    [](boss::Symbol const& symbol) { return symbol.getName(); },
                    [](std::string const& str) { return str; }),
                args.at(idx));
    }
    return accStr;
  }(expr0);
  CHECK(str == "List_howdie_1_unknown_hello world");
}

TEST_CASE("Merge two complex expressions", "[expressions]") {
  auto delimeters = "List"_("_"_(), "_"_(), "_"_(), "_"_());
  auto expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  auto delimetersIt = std::make_move_iterator(delimeters.getArguments().begin());
  auto delimetersItEnd = std::make_move_iterator(delimeters.getArguments().end());
  auto exprIt = std::make_move_iterator(expr.getArguments().begin());
  auto exprItEnd = std::make_move_iterator(expr.getArguments().end());
  auto args = boss::ExpressionArguments();
  for(; delimetersIt != delimetersItEnd && exprIt != exprItEnd; ++delimetersIt, ++exprIt) {
    args.emplace_back(std::move(*delimetersIt));
    args.emplace_back(std::move(*exprIt));
  }
  auto e = boss::ComplexExpression("List"_, std::move(args));
  auto str = std::accumulate(
      e.getArguments().begin(), e.getArguments().end(), e.getHead().getName(),
      [](auto const& accStr, auto const& arg) {
        return accStr + visit(boss::utilities::overload(
                                  [](auto const& value) { return std::to_string(value); },
                                  [](boss::ComplexExpression const& expr) {
                                    return expr.getHead().getName();
                                  },
                                  [](boss::Symbol const& symbol) { return symbol.getName(); },
                                  [](std::string const& str) { return str; }),
                              arg);
      });
  CHECK(str == "List_howdie_1_unknown_hello world");
}

TEST_CASE("Merge a static and a dynamic complex expressions", "[expressions]") {
  auto delimeters = "List"_("_"s, "_"s, "_"s, "_"s);
  auto expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  auto delimetersIt = std::make_move_iterator(delimeters.getArguments().begin());
  auto delimetersItEnd = std::make_move_iterator(delimeters.getArguments().end());
  auto exprIt = std::make_move_iterator(expr.getArguments().begin());
  auto exprItEnd = std::make_move_iterator(expr.getArguments().end());
  auto args = boss::ExpressionArguments();
  for(; delimetersIt != delimetersItEnd && exprIt != exprItEnd; ++delimetersIt, ++exprIt) {
    args.emplace_back(std::move(*delimetersIt));
    args.emplace_back(std::move(*exprIt));
  }
  auto e = boss::ComplexExpression("List"_, std::move(args));
  auto str = std::accumulate(
      e.getArguments().begin(), e.getArguments().end(), e.getHead().getName(),
      [](auto const& accStr, auto const& arg) {
        return accStr + visit(boss::utilities::overload(
                                  [](auto const& value) { return std::to_string(value); },
                                  [](boss::ComplexExpression const& expr) {
                                    return expr.getHead().getName();
                                  },
                                  [](boss::Symbol const& symbol) { return symbol.getName(); },
                                  [](std::string const& str) { return str; }),
                              arg);
      });
  CHECK(str == "List_howdie_1_unknown_hello world");
}

TEST_CASE("holds_alternative for complex expression's arguments", "[expressions]") {
  auto const& expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  CHECK(holds_alternative<boss::ComplexExpression>(expr.getArguments().at(0)));
  CHECK(holds_alternative<int64_t>(expr.getArguments().at(1)));
  CHECK(holds_alternative<boss::Symbol>(expr.getArguments().at(2)));
  CHECK(holds_alternative<std::string>(expr.getArguments().at(3)));
}

TEST_CASE("get_if for complex expression's arguments", "[expressions]") {
  auto const& expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  auto const& arg0 = expr.getArguments().at(0);
  auto const& arg1 = expr.getArguments().at(1);
  auto const& arg2 = expr.getArguments().at(2);
  auto const& arg3 = expr.getArguments().at(3);
  CHECK(get_if<boss::ComplexExpression>(&arg0) != nullptr);
  CHECK(get_if<int64_t>(&arg1) != nullptr);
  CHECK(get_if<boss::Symbol>(&arg2) != nullptr);
  CHECK(get_if<std::string>(&arg3) != nullptr);
  auto const& arg0args = get<boss::ComplexExpression>(arg0).getArguments();
  CHECK(arg0args.empty());
}

TEST_CASE("move expression's arguments to a new expression", "[expressions]") {
  auto expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  auto&& movedExpr = std::move(expr);
  boss::ExpressionArguments args = movedExpr.getArguments();
  auto expr2 = boss::ComplexExpression(std::move(movedExpr.getHead()), std::move(args)); // NOLINT
  CHECK(get<boss::ComplexExpression>(expr2.getArguments().at(0)) == "howdie"_());
  CHECK(get<int64_t>(expr2.getArguments().at(1)) == 1);
  CHECK(get<boss::Symbol>(expr2.getArguments().at(2)) == "unknown"_);
  CHECK(get<std::string>(expr2.getArguments().at(3)) == "hello world"s);
}

TEST_CASE("copy expression's arguments to a new expression", "[expressions]") {
  auto expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  auto args =
      expr.getArguments(); // TODO: this one gets the reference to the arguments
                           // when it should be a copy.
                           // Any modification/move of args will be reflected in expr's arguments!
  get<int64_t>(args.at(1)) = 2;
  auto expr2 = boss::ComplexExpression(expr.getHead(), args);
  get<int64_t>(args.at(1)) = 3;
  auto expr3 = boss::ComplexExpression(expr.getHead(), std::move(args)); // NOLINT
  // CHECK(get<int64_t>(expr.getArguments().at(1)) == 1); // fails for now (see above TODO)
  CHECK(get<int64_t>(expr2.getArguments().at(1)) == 2);
  CHECK(get<int64_t>(expr3.getArguments().at(1)) == 3);
}

TEST_CASE("copy non-const expression's arguments to ExpressionArguments", "[expressions]") {
  auto expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  boss::ExpressionArguments args = expr.getArguments(); // TODO: why is it moved?
  get<int64_t>(args.at(1)) = 2;
  auto expr2 = boss::ComplexExpression(expr.getHead(), std::move(args));
  // CHECK(get<int64_t>(expr.getArguments().at(1)) == 1); // fails because args was moved (see TODO)
  CHECK(get<int64_t>(expr2.getArguments().at(1)) == 2);
}

TEST_CASE("copy const expression's arguments to ExpressionArguments)", "[expressions]") {
  auto const& expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  boss::ExpressionArguments args = expr.getArguments();
  get<int64_t>(args.at(1)) = 2;
  auto expr2 = boss::ComplexExpression(expr.getHead(), std::move(args));
  CHECK(get<int64_t>(expr.getArguments().at(1)) == 1);
  CHECK(get<int64_t>(expr2.getArguments().at(1)) == 2);
}

TEST_CASE("move and dispatch expression's arguments", "[expressions]") {
  auto expr = "List"_("howdie"_(), 1, "unknown"_, "hello world"s);
  std::vector<boss::Symbol> symbols;
  std::vector<boss::Expression> otherExpressions;
  for(auto&& arg : (boss::ExpressionArguments)std::move(expr).getArguments()) {
    visit(boss::utilities::overload(
              [&otherExpressions](auto&& value) {
                otherExpressions.emplace_back(std::forward<decltype(value)>(value));
              },
              [&symbols](boss::Symbol&& symbol) { symbols.emplace_back(std::move(symbol)); }),
          std::move(arg));
  }
  CHECK(symbols.size() == 1);
  CHECK(symbols[0] == "unknown"_);
  CHECK(otherExpressions.size() == 3);
}

// NOLINTNEXTLINE
TEMPLATE_TEST_CASE("Complex Expressions with numeric Spans", "[spans]", std::int64_t,
                   std::double_t) {
  auto input = GENERATE(take(3, chunk(5, random<TestType>(1, 1000))));
  auto v = vector<TestType>(input);
  auto s = boss::Span<TestType>(std::move(v));
  auto vectorExpression = "duh"_(std::move(s));
  REQUIRE(vectorExpression.getArguments().size() == input.size());
  for(auto i = 0U; i < input.size(); i++) {
    CHECK(vectorExpression.getArguments().at(i) == input.at(i));
    CHECK(vectorExpression.getArguments()[i] == input[i]);
  }
}

// NOLINTNEXTLINE
TEMPLATE_TEST_CASE("Complex Expressions with non-owning numeric Spans", "[spans]", std::int64_t,
                   std::double_t) {
  auto input = GENERATE(take(3, chunk(5, random<TestType>(1, 1000))));
  auto v = vector<TestType>(input);
  auto s = boss::Span<TestType>(v);
  auto vectorExpression = "duh"_(std::move(s));
  REQUIRE(vectorExpression.getArguments().size() == input.size());
  for(auto i = 0U; i < input.size(); i++) {
    CHECK(vectorExpression.getArguments().at(i) == input.at(i));
    CHECK(vectorExpression.getArguments()[i] == input[i]);
  }
}

// NOLINTNEXTLINE
TEMPLATE_TEST_CASE("Complex Expressions with non-owning const numeric Spans", "[spans]",
                   std::int64_t, std::double_t) {
  auto input = GENERATE(take(3, chunk(5, random<TestType>(1, 1000))));
  auto const v = vector<TestType>(input);
  auto s = boss::Span<TestType const>(v);
  auto const vectorExpression = "duh"_(std::move(s));
  REQUIRE(vectorExpression.getArguments().size() == input.size());
  for(auto i = 0U; i < input.size(); i++) {
    CHECK(vectorExpression.getArguments().at(i) == input.at(i));
    CHECK(vectorExpression.getArguments()[i] == input[i]);
  }
}

// NOLINTNEXTLINE
TEMPLATE_TEST_CASE("Complex Expressions with numeric Arrow Spans", "[spans][arrow]", std::int64_t,
                   std::double_t) {
  auto input = GENERATE(take(3, chunk(5, random<TestType>(1, 1000))));
  std::conditional_t<std::is_same_v<TestType, std::int64_t>, arrow::Int64Builder,
                     arrow::DoubleBuilder>
      builder;
  auto status = builder.AppendValues(begin(input), end(input));
  auto thingy = builder.Finish().ValueOrDie();
  auto* v = thingy->data()->template GetMutableValues<TestType>(1);
  auto s = boss::Span<TestType>(v, thingy->length(), [thingy](void* /* unused */) {});
  auto vectorExpression = "duh"_(std::move(s));
  REQUIRE(vectorExpression.getArguments().size() == input.size());
  for(auto i = 0U; i < input.size(); i++) {
    CHECK(vectorExpression.getArguments().at(i) == input.at(i));
    CHECK(vectorExpression.getArguments()[i] == input[i]);
  }
}

// NOLINTNEXTLINE
TEMPLATE_TEST_CASE("Cloning Expressions with numeric Spans", "[spans][clone]", std::int64_t,
                   std::double_t) {
  auto input = GENERATE(take(3, chunk(5, random<TestType>(1, 1000))));
  auto vectorExpression = "duh"_(boss::Span<TestType>(vector(input)));
  auto clonedVectorExpression = vectorExpression.clone(CloneReason::FOR_TESTING);
  for(auto i = 0U; i < input.size(); i++) {
    CHECK(clonedVectorExpression.getArguments().at(i) == input.at(i));
    CHECK(vectorExpression.getArguments()[i] == input[i]);
  }
}

// NOLINTNEXTLINE
TEMPLATE_TEST_CASE("Complex Expressions with Spans", "[spans]", std::string, boss::Symbol) {
  using std::literals::string_literals::operator""s;
  auto vals = GENERATE(take(3, chunk(5, values({"a"s, "b"s, "c"s, "d"s, "e"s, "f"s, "g"s, "h"s}))));
  auto input = vector<TestType>();
  std::transform(begin(vals), end(vals), std::back_inserter(input),
                 [](auto v) { return TestType(v); });
  auto vectorExpression = "duh"_(boss::Span<TestType>(std::move(input)));
  for(auto i = 0U; i < vals.size(); i++) {
    CHECK(vectorExpression.getArguments().at(0) == TestType(vals.at(0)));
    CHECK(vectorExpression.getArguments()[0] == TestType(vals[0]));
  }
}

TEST_CASE("Basics", "[basics]") { // NOLINT
  auto engine = boss::engines::BootstrapEngine();
  REQUIRE(!librariesToTest.empty());
  auto eval = [&engine](boss::Expression&& expression) mutable {
    return engine.evaluate("EvaluateInEngines"_("List"_(GENERATE(from_range(librariesToTest))),
                                                std::move(expression)));
  };

  SECTION("CatchingErrors") {
    CHECK_THROWS_MATCHES(
        engine.evaluate("EvaluateInEngines"_("List"_(9), 5)), std::bad_variant_access,
        Message("expected and actual type mismatch in expression \"9\", expected string"));
  }

  SECTION("Atomics") {
    CHECK(get<std::int64_t>(eval(boss::Expression(9))) == 9); // NOLINT
  }

  SECTION("Addition") {
    CHECK(get<std::int64_t>(eval("Plus"_(5, 4))) == 9); // NOLINT
    CHECK(get<std::int64_t>(eval("Plus"_(5, 2, 2))) == 9);
    CHECK(get<std::int64_t>(eval("Plus"_(5, 2, 2))) == 9);
    CHECK(get<std::int64_t>(eval("Plus"_("Plus"_(2, 3), 2, 2))) == 9);
    CHECK(get<std::int64_t>(eval("Plus"_("Plus"_(3, 2), 2, 2))) == 9);
  }

  SECTION("Strings") {
    CHECK(get<string>(eval("StringJoin"_((string) "howdie", (string) " ", (string) "world"))) ==
          "howdie world");
  }

  SECTION("Doubles") {
    auto const twoAndAHalf = 2.5F;
    auto const two = 2.0F;
    auto const quantum = 0.001F;
    CHECK(std::fabs(get<double>(eval("Plus"_(twoAndAHalf, twoAndAHalf))) - two * twoAndAHalf) <
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
    CHECK(get<std::int64_t>(expression.getArguments().at(0)) == 9);

    CHECK(get<std::string>(
              get<boss::ComplexExpression>(eval("UndefinedFunction"_((string) "Hello World!")))
                  .getArguments()
                  .at(0)) == "Hello World!");
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
  SECTION("Relational on ephemeral tables") {

    SECTION("Selection") {
      auto const& result =
          eval("Select"_("ScanColumns"_("Column"_("Size"_, "List"_(2, 3, 1, 4, 1))),
                         "Where"_("Greater"_("Size"_, 3))));
      REQUIRE(result == "List"_("List"_(4)));
    }
  }

  SECTION("Relational (simple)") {
    eval("CreateTable"_("Customer"_, "FirstName"_, "LastName"_));
    eval("InsertInto"_("Customer"_, "John", "McCarthy"));
    eval("InsertInto"_("Customer"_, "Sam", "Madden"));
    eval("InsertInto"_("Customer"_, "Barbara", "Liskov"));
    SECTION("Selection") {
      auto sam = eval(
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

    REQUIRE(get<std::int64_t>(eval("Length"_("Select"_("Customer"_, "Function"_(true))))) == 0);
    auto emptyTable = eval("Select"_("Customer"_, "Function"_(true)));
    auto tmp = eval("Length"_(std::move(emptyTable)));
    CHECK(get<std::int64_t>(tmp) == 0);
    eval("InsertInto"_("Customer"_, 1, "John", "McCarthy", 1927, "USA"));  // NOLINT
    eval("InsertInto"_("Customer"_, 2, "Sam", "Madden", 1976, "USA"));     // NOLINT
    eval("InsertInto"_("Customer"_, 3, "Barbara", "Liskov", 1939, "USA")); // NOLINT
    INFO("Select"_("Customer"_, "Function"_(true)));
    CHECK(eval("Length"_("Select"_("Customer"_, "Function"_(true)))) == Expression(3));
    auto fullTable = eval("Select"_("Customer"_, "Function"_(true)));
    auto tmp2 = eval("Length"_(std::move(fullTable)));
    CHECK(get<std::int64_t>(tmp2) == 3);
    CHECK(get<std::string>(eval("Extract"_("Extract"_("Select"_("Customer"_, "Function"_(true)), 2),
                                           3))) == "Madden");

    SECTION("Selection") {
      auto sam = eval("Select"_(
          "Customer"_,
          "Function"_("List"_("tuple"_), "StringContainsQ"_("Madden", "Column"_("tuple"_, 3)))));
      CHECK(get<std::int64_t>(eval("Length"_(sam.clone(CloneReason::FOR_TESTING)))) == 1);
      auto samRow = eval("Extract"_(std::move(sam), 1));
      CHECK(get<std::int64_t>(eval("Length"_(samRow.clone(CloneReason::FOR_TESTING)))) == 5);
      CHECK(get<string>(eval("Extract"_(samRow.clone(CloneReason::FOR_TESTING), 2))) == "Sam");
      auto tmp = eval("Extract"_(std::move(samRow), 3));
      CHECK(get<string>(tmp) == "Madden");
      auto none = eval("Select"_("Customer"_, "Function"_(false)));
      auto tmp2 = eval("Length"_(std::move(none)));
      CHECK(get<std::int64_t>(tmp2) == 0);
      auto all = eval("Select"_("Customer"_, "Function"_(true)));
      CHECK(get<std::int64_t>(eval("Length"_(all.clone(CloneReason::FOR_TESTING)))) == 3);
      auto johnRow = eval("Extract"_(all.clone(CloneReason::FOR_TESTING), 1));
      auto barbaraRow = eval("Extract"_(std::move(all), 3));
      auto tmp3 = eval("Extract"_(std::move(johnRow), 2));
      CHECK(get<string>(tmp3) == "John");
      auto tmp4 = eval("Extract"_(std::move(barbaraRow), 2));
      CHECK(get<string>(tmp4) == "Barbara");
    }

    SECTION("Projection") {
      auto fullnames = eval(
          "Project"_("Customer"_, "As"_("FirstName"_, "FirstName"_, "LastName"_, "LastName"_)));
      INFO("Project"_("Customer"_, "As"_("FirstName"_, "FirstName"_, "LastName"_, "LastName"_)));
      INFO(fullnames);
      CHECK(get<std::int64_t>(eval("Length"_(fullnames.clone(CloneReason::FOR_TESTING)))) == 3);
      auto firstNames = eval("Project"_("Customer"_, "As"_("FirstName"_, "FirstName"_)));
      INFO(eval("Extract"_("Extract"_(fullnames.clone(CloneReason::FOR_TESTING), 1), 1)));
      auto tmp = eval("Extract"_("Extract"_(std::move(firstNames), 1), 1));
      CHECK(get<string>(tmp) == get<string>(eval("Extract"_(
                                    "Extract"_(fullnames.clone(CloneReason::FOR_TESTING), 1), 1))));
      auto lastNames = eval("Project"_("Customer"_, "As"_("LastName"_, "LastName"_)));
      INFO("lastnames=" << eval(
               "Extract"_("Extract"_(lastNames.clone(CloneReason::FOR_TESTING), 1), 1)));
      INFO("fullnames=" << eval(
               "Extract"_("Extract"_(fullnames.clone(CloneReason::FOR_TESTING), 1), 2)));
      auto tmp2 = eval("Extract"_("Extract"_(std::move(lastNames), 1), 1));
      auto tmp3 = eval("Extract"_("Extract"_(std::move(fullnames), 1), 2));
      CHECK(get<string>(tmp2) == get<string>(tmp3));
    }

    SECTION("Sorting") {
      auto sortedByLastName = eval("SortBy"_("Select"_("Customer"_, "Function"_(true)),
                                             "Function"_("tuple"_, "Column"_("tuple"_, 3))));
      auto liskovRow = eval("Extract"_(sortedByLastName.clone(CloneReason::FOR_TESTING), 1));
      auto MaddenRow = eval("Extract"_(std::move(sortedByLastName), 2));
      auto tmp = eval("Extract"_(std::move(liskovRow), 3));
      CHECK(get<string>(tmp) == "Liskov");
      auto tmp2 = eval("Extract"_(std::move(MaddenRow), 3));
      CHECK(get<string>(tmp2) == "Madden");
    }

    SECTION("Aggregation") {
      auto countRows = eval("Group"_("Customer"_, "Function"_(0), "Count"_));
      INFO("countRows=" << countRows << "\n"
                        << eval("Extract"_(
                               "Extract"_(countRows.clone(CloneReason::FOR_TESTING), 1))));
      auto tmp = eval("Extract"_("Extract"_(std::move(countRows), 1), 1));
      CHECK(get<std::int64_t>(tmp) == 3);
      CHECK(get<std::int64_t>(eval("Extract"_(
                "Extract"_("Group"_(("Select"_("Customer"_, "Where"_("StringContainsQ"_(
                                                                "Madden", "LastName"_)))),
                                    "Function"_(0), "Count"_),
                           1),
                1))) == 1);
    }
  }
}

TEST_CASE("Arrays", "[arrays]") { // NOLINT
  auto engine = boss::engines::BootstrapEngine();
  namespace nasty = boss::utilities::nasty;
  REQUIRE(!librariesToTest.empty());
  auto eval = [&engine](auto&& expression) mutable {
    return engine.evaluate("EvaluateInEngines"_("List"_(GENERATE(from_range(librariesToTest))),
                                                std::forward<decltype(expression)>(expression)));
  };

  std::vector<int64_t> ints{10, 20, 30, 40, 50}; // NOLINT
  std::shared_ptr<arrow::Array> arrayPtr(
      new arrow::Int64Array((long long)ints.size(), arrow::Buffer::Wrap(ints)));

  auto arrayPtrExpr = nasty::arrowArrayToExpression(arrayPtr);
  eval("CreateTable"_("Thingy"_, "Value"_));
  eval("AttachColumns"_("Thingy"_, arrayPtrExpr));

  SECTION("ArrowArrays") {
    CHECK(get<std::int64_t>(eval("Extract"_(arrayPtrExpr, 1))) == 10);
    CHECK(get<std::int64_t>(eval("Extract"_(arrayPtrExpr, 2))) == 20);
    CHECK(get<std::int64_t>(eval("Extract"_(arrayPtrExpr, 3))) == 30);
    CHECK(get<std::int64_t>(eval("Extract"_(arrayPtrExpr, 4))) == 40);
    CHECK(get<std::int64_t>(eval("Extract"_(arrayPtrExpr, 5))) == 50);
    CHECK(eval(arrayPtrExpr) == "List"_(10, 20, 30, 40, 50));
  }

  auto compareColumn = [&eval](boss::Expression&& expression, auto const& results) {
    for(auto i = 0; i < results.size(); i++) {
      auto tmp = eval("Extract"_("Extract"_(expression.clone(CloneReason::FOR_TESTING), i + 1), 1));
      CHECK(get<typename std::remove_reference_t<decltype(results)>::value_type>(tmp) ==
            results[i]);
    }
  };

  SECTION("Plus") {
    compareColumn("Project"_("Thingy"_, "As"_("Result"_, "Plus"_("Value"_, "Value"_))),
                  vector<std::int64_t>{20, 40, 60, 80, 100}); // NOLINT(readability-magic-numbers)
    compareColumn("Project"_("Thingy"_, "As"_("Result"_, "Plus"_("Value"_, 1))),
                  vector<std::int64_t>{11, 21, 31, 41, 51}); // NOLINT(readability-magic-numbers)
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

// NOLINTNEXTLINE
TEMPLATE_TEST_CASE("Summation of numeric Spans", "[spans]", std::int64_t, std::double_t) {
  auto engine = boss::engines::BootstrapEngine();
  REQUIRE(!librariesToTest.empty());
  auto eval = [&engine](auto&& expression) mutable {
    return engine.evaluate("EvaluateInEngines"_("List"_(GENERATE(from_range(librariesToTest))),
                                                std::forward<decltype(expression)>(expression)));
  };

  auto input = GENERATE(take(3, chunk(50, random<TestType>(1, 1000))));
  auto sum = std::accumulate(begin(input), end(input), TestType());

  if constexpr(std::is_same_v<TestType, std::double_t>) {
    CHECK(get<std::double_t>(eval("Plus"_(boss::Span<TestType>(vector(input))))) ==
          Catch::Detail::Approx((std::double_t)sum));
  } else {
    CHECK(get<TestType>(eval("Plus"_(boss::Span<TestType>(vector(input))))) == sum);
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
