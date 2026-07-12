#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../Source/BOSS.h"

TEST_CASE("Build Expression", "[api]") {
  auto input = (std::array {longToNewBOSSExpression(3), longToNewBOSSExpression(4)});
  auto* s = symbolNameToNewBOSSSymbol("Plus");
  auto* c = newComplexBOSSExpression(s, 2, input.data());
  auto* res = BOSSEvaluate(c);
  auto* result = getArgumentsFromBOSSExpression(res);
  auto secondArgument = getLongValueFromBOSSExpression(result[1]);
  freeBOSSSymbol(s);
  freeBOSSExpression(res);
  freeBOSSExpression(input[0]);
  freeBOSSExpression(input[1]);
  freeBOSSArguments(result);
  CHECK(secondArgument == 4);
}

TEST_CASE("Build expression, with strings", "[api]") {
  auto input = (std::array {stringToNewBOSSExpression("test string")});
  auto* s = symbolNameToNewBOSSSymbol("UnevaluatedAsNoEngineIsSet");
  auto* c = newComplexBOSSExpression(s, 1, input.data());
  auto* res = BOSSEvaluate(c);
  auto* result = getArgumentsFromBOSSExpression(res);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  char* argument1 = getNewStringValueFromBOSSExpression(result[0]);
  auto const str1 = std::string(argument1);
  auto const str2 = std::string("test string");
  freeBOSSSymbol(s);
  freeBOSSString(argument1);
  freeBOSSExpression(res);
  freeBOSSExpression(input[0]);
  freeBOSSArguments(result);
  CHECK(str1 == str2);
}

namespace {
// Wraps a freshly-made span as (Column :spans <span>), runs the shared structural and
// zero-copy checks, and hands the flattened arguments to a caller-supplied element check.
// Frees the span wrapper, the expression, and the moved-out span.
template <typename CheckElements>
void checkSpanRoundTrip(struct BOSSExpressionSpan* span, size_t expectedElementCount,
                        CheckElements checkElements) {
  auto const addressBefore = getBOSSSpanBeginAddress(span);
  auto* head = symbolNameToNewBOSSSymbol("Column");
  auto noArguments = std::array<struct BOSSExpression*, 1> {};
  auto spans = std::array<struct BOSSExpressionSpan*, 1> {span};
  auto* expr = newComplexBOSSExpressionWithSpans(head, 0, noArguments.data(), 1, spans.data());
  freeBOSSSymbol(head);
  freeBOSSExpressionSpan(span); // payload was moved into expr; free the empty wrapper

  CHECK(getDynamicArgumentCountFromBOSSExpression(expr) == 0);
  CHECK(getSpanArgumentCountFromBOSSExpression(expr) == 1);
  // the span elements are reachable as (flattened) arguments of the holding expression
  CHECK(getArgumentCountFromBOSSExpression(expr) == expectedElementCount);
  auto** flattened = getArgumentsFromBOSSExpression(expr);
  checkElements(flattened);
  freeBOSSArguments(flattened);

  // moving the span out preserves the underlying buffer address: zero-copy end to end
  auto** movedSpans = getSpanArgumentsFromBOSSExpression(expr);
  CHECK(getBOSSSpanBeginAddress(movedSpans[0]) == addressBefore);
  CHECK(getSpanArgumentCountFromBOSSExpression(expr) == 0); // moved out
  freeBOSSExpressionSpan(movedSpans[0]);
  freeBOSSSpanArray(movedSpans);
  freeBOSSExpression(expr);
}
} // namespace

TEST_CASE("Span arguments round-trip zero-copy across element types", "[api]") {
  SECTION("int8 with embedded null byte") {
    auto const values = std::array<int8_t, 5> {97, 98, 0, 99, -1};
    checkSpanRoundTrip(makeInt8BOSSSpan(values.data(), values.size()), values.size(),
                       [](BOSSExpression** flattened) {
                         CHECK(getCharValueFromBOSSExpression(flattened[0]) == 97);
                         CHECK(getCharValueFromBOSSExpression(flattened[2]) == 0);
                       });
  }
  SECTION("int32") {
    auto const values = std::array<int32_t, 3> {7, -8, 9};
    checkSpanRoundTrip(makeInt32BOSSSpan(values.data(), values.size()), values.size(),
                       [](BOSSExpression** flattened) {
                         CHECK(getIntValueFromBOSSExpression(flattened[1]) == -8);
                       });
  }
  SECTION("double") {
    auto const values = std::array<double, 2> {1.5, -2.25};
    checkSpanRoundTrip(makeDoubleBOSSSpan(values.data(), values.size()), values.size(),
                       [](BOSSExpression** flattened) {
                         CHECK(getDoubleValueFromBOSSExpression(flattened[0]) == 1.5);
                       });
  }
  SECTION("string") {
    auto const values = std::array<char const*, 2> {"foo", "barbaz"};
    checkSpanRoundTrip(makeStringBOSSSpan(values.data(), values.size()), values.size(),
                       [](BOSSExpression** flattened) {
                         char* first = getNewStringValueFromBOSSExpression(flattened[1]);
                         CHECK(std::string(first) == "barbaz");
                         freeBOSSString(first);
                       });
  }
}
