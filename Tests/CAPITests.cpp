#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "../Source/BOSS.h"
#include "../Source/PortableBOSSSerialization.h"

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

TEST_CASE("C-API round-trips large strings (200KB)", "[api][largestring]") {
  auto const largeString = std::string(200UL * 1024, 'a');

  SECTION("large string atom") {
    auto* expression = stringToNewBOSSExpression(largeString.c_str());
    char* roundTripped = getNewStringValueFromBOSSExpression(expression);
    auto const result = std::string(roundTripped);
    freeBOSSString(roundTripped);
    freeBOSSExpression(expression);
    CHECK(result.size() == largeString.size());
    CHECK(result == largeString);
  }

  SECTION("large string as a complex-expression argument") {
    auto input = std::array {stringToNewBOSSExpression(largeString.c_str())};
    auto* head = symbolNameToNewBOSSSymbol("UnevaluatedAsNoEngineIsSet");
    auto* complexExpression = newComplexBOSSExpression(head, 1, input.data());
    auto* evaluated = BOSSEvaluate(complexExpression);
    auto** arguments = getArgumentsFromBOSSExpression(evaluated);
    char* roundTripped = getNewStringValueFromBOSSExpression(arguments[0]);
    auto const result = std::string(roundTripped);
    freeBOSSSymbol(head);
    freeBOSSString(roundTripped);
    freeBOSSExpression(evaluated);
    freeBOSSExpression(input[0]);
    freeBOSSArguments(arguments);
    CHECK(result.size() == largeString.size());
    CHECK(result == largeString);
  }

  SECTION("large string as an expression head (symbol)") {
    auto* symbol = symbolNameToNewBOSSSymbol(largeString.c_str());
    // bossSymbolToNewString returns an owning buffer (strdup) despite its char const*
    // signature, so take ownership once here and treat it as the char* we must free.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    auto* roundTripped = const_cast<char*>(bossSymbolToNewString(symbol));
    auto const result = std::string(roundTripped);
    freeBOSSSymbol(symbol);
    freeBOSSString(roundTripped);
    CHECK(result.size() == largeString.size());
    CHECK(result == largeString);
  }
}

namespace {
// Wraps a freshly-made span in a Column expression carrying no dynamic arguments and a
// single span argument, runs the shared structural and zero-copy checks, and hands the
// flattened arguments to a caller-supplied element check.
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

TEST_CASE("Span arguments round-trip zero-copy: int8 with embedded null byte", "[api]") {
  auto const values = std::array<int8_t, 5> {97, 98, 0, 99, -1};
  checkSpanRoundTrip(makeInt8BOSSSpan(values.data(), values.size()), values.size(),
                     [](BOSSExpression** flattened) {
                       CHECK(getCharValueFromBOSSExpression(flattened[0]) == 97);
                       CHECK(getCharValueFromBOSSExpression(flattened[2]) == 0);
                     });
}

TEST_CASE("Span arguments round-trip zero-copy: int32", "[api]") {
  auto const values = std::array<int32_t, 3> {7, -8, 9};
  checkSpanRoundTrip(
      makeInt32BOSSSpan(values.data(), values.size()), values.size(),
      [](BOSSExpression** flattened) { CHECK(getIntValueFromBOSSExpression(flattened[1]) == -8); });
}

TEST_CASE("Span arguments round-trip zero-copy: double", "[api]") {
  constexpr auto FIRST_VALUE = 1.5;
  auto const values = std::array<double, 2> {FIRST_VALUE, -2.25};
  checkSpanRoundTrip(makeDoubleBOSSSpan(values.data(), values.size()), values.size(),
                     [](BOSSExpression** flattened) {
                       CHECK(getDoubleValueFromBOSSExpression(flattened[0]) == FIRST_VALUE);
                     });
}

TEST_CASE("Span constructors return null for null buffers and null elements", "[api]") {
  // Nulls in, nulls out: a null buffer yields no span at all, across the whole exported surface.
  for(auto* span :
      {makeBoolBOSSSpan(nullptr, 1), makeInt8BOSSSpan(nullptr, 2), makeInt32BOSSSpan(nullptr, 3),
       makeInt64BOSSSpan(nullptr, 4), makeFloatBOSSSpan(nullptr, 5), makeDoubleBOSSSpan(nullptr, 2),
       makeStringBOSSSpan(nullptr, 4), makeSymbolBOSSSpan(nullptr, 6)}) {
    CHECK(span == nullptr);
  }

  // A null element is not substituted either -- the whole call yields no span.
  auto const withNullElement = std::array<char const*, 2> {nullptr, "value"};
  CHECK(makeStringBOSSSpan(withNullElement.data(), withNullElement.size()) == nullptr);
  CHECK(makeSymbolBOSSSpan(withNullElement.data(), withNullElement.size()) == nullptr);

  // An all-non-null array still builds normally.
  auto const values = std::array<char const*, 2> {"first", "value"};
  auto* span = makeStringBOSSSpan(values.data(), values.size());
  REQUIRE(span != nullptr);
  freeBOSSExpressionSpan(span);

  // A null span is a legal argument to the address accessor, and reports "no address".
  CHECK(getBOSSSpanBeginAddress(nullptr) == 0);
}

TEST_CASE("newComplexBOSSExpressionWithSpans tolerates a null span array", "[api]") {
  auto noArguments = std::array<struct BOSSExpression*, 1> {};
  // A null span array contributes no spans, whatever spanCount claims.
  auto* head = symbolNameToNewBOSSSymbol("Column");
  auto* expr = newComplexBOSSExpressionWithSpans(head, 0, noArguments.data(), 3, nullptr);
  freeBOSSSymbol(head);
  CHECK(getSpanArgumentCountFromBOSSExpression(expr) == 0);
  CHECK(getArgumentCountFromBOSSExpression(expr) == 0);
  freeBOSSExpression(expr);

  // A null entry inside the array is skipped rather than dereferenced.
  auto const values = std::array<int32_t, 2> {5, 6};
  auto spans = std::array<struct BOSSExpressionSpan*, 2> {
      nullptr, makeInt32BOSSSpan(values.data(), values.size())};
  auto* head2 = symbolNameToNewBOSSSymbol("Column");
  auto* expr2 = newComplexBOSSExpressionWithSpans(head2, 0, noArguments.data(), 2, spans.data());
  freeBOSSSymbol(head2);
  freeBOSSExpressionSpan(spans[1]);
  CHECK(getSpanArgumentCountFromBOSSExpression(expr2) == 1);
  CHECK(getArgumentCountFromBOSSExpression(expr2) == values.size());
  freeBOSSExpression(expr2);
}

TEST_CASE("Span arguments round-trip zero-copy: string", "[api]") {
  auto const values = std::array<char const*, 2> {"foo", "barbaz"};
  checkSpanRoundTrip(makeStringBOSSSpan(values.data(), values.size()), values.size(),
                     [](BOSSExpression** flattened) {
                       char* first = getNewStringValueFromBOSSExpression(flattened[1]);
                       CHECK(std::string(first) == "barbaz");
                       freeBOSSString(first);
                     });
}

TEST_CASE("C API functions answer a null pointer without dereferencing it", "[api]") {
  CHECK(BOSSEvaluate(nullptr) == nullptr);
  CHECK(stringToNewBOSSExpression(nullptr) == nullptr);
  CHECK(symbolNameToNewBOSSExpression(nullptr) == nullptr);
  CHECK(symbolNameToNewBOSSSymbol(nullptr) == nullptr);
  CHECK(bossSymbolToNewString(nullptr) == nullptr);
  auto* head = symbolNameToNewBOSSSymbol("Head");
  auto nullArgument = std::array<BOSSExpression*, 1> {nullptr};
  CHECK(newComplexBOSSExpression(nullptr, 0, nullptr) == nullptr);
  CHECK(newComplexBOSSExpression(head, 1, nullptr) == nullptr);
  CHECK(newComplexBOSSExpression(head, 1, nullArgument.data()) == nullptr);
  CHECK(newComplexBOSSExpressionWithSpans(nullptr, 0, nullptr, 0, nullptr) == nullptr);
  CHECK(newComplexBOSSExpressionWithSpans(head, 1, nullArgument.data(), 0, nullptr) == nullptr);
  freeBOSSSymbol(head);
  CHECK(getDynamicArgumentCountFromBOSSExpression(nullptr) == 0);
  CHECK(getDynamicArgumentsFromBOSSExpression(nullptr) == nullptr);
  CHECK(getSpanArgumentCountFromBOSSExpression(nullptr) == 0);
  CHECK(getSpanArgumentsFromBOSSExpression(nullptr) == nullptr);
  CHECK(getBOSSExpressionTypeID(nullptr) == 0);
  CHECK_FALSE(getBoolValueFromBOSSExpression(nullptr));
  CHECK(getCharValueFromBOSSExpression(nullptr) == 0);
  CHECK(getIntValueFromBOSSExpression(nullptr) == 0);
  CHECK(getLongValueFromBOSSExpression(nullptr) == 0);
  CHECK(getFloatValueFromBOSSExpression(nullptr) == 0);
  CHECK(getDoubleValueFromBOSSExpression(nullptr) == 0);
  CHECK(getNewStringValueFromBOSSExpression(nullptr) == nullptr);
  CHECK(getNewSymbolNameFromBOSSExpression(nullptr) == nullptr);
  CHECK(getHeadFromBOSSExpression(nullptr) == nullptr);
  CHECK(getArgumentCountFromBOSSExpression(nullptr) == 0);
  CHECK(getArgumentsFromBOSSExpression(nullptr) == nullptr);
  freeBOSSArguments(nullptr);
  CHECK(serializeBOSSExpression(nullptr) == nullptr);
  CHECK(deserializeBOSSExpression(nullptr) == nullptr);
}

TEST_CASE("Serialization helpers answer a null pointer without dereferencing it", "[api]") {
  CHECK(getExpressionArguments(nullptr) == nullptr);
  CHECK(getArgumentTypes(nullptr) == nullptr);
  CHECK(getExpressionSubexpressions(nullptr) == nullptr);
  CHECK(getStringBuffer(nullptr) == nullptr);
  CHECK(makeBoolArgument(nullptr, 0) == nullptr);
  CHECK(makeCharArgument(nullptr, 0) == nullptr);
  CHECK(makeIntArgument(nullptr, 0) == nullptr);
  CHECK(makeLongArgument(nullptr, 0) == nullptr);
  CHECK(makeFloatArgument(nullptr, 0) == nullptr);
  CHECK(makeDoubleArgument(nullptr, 0) == nullptr);
  CHECK(makeStringArgument(nullptr, 0) == nullptr);
  CHECK(makeSymbolArgument(nullptr, 0) == nullptr);
  CHECK(makeExpressionArgument(nullptr, 0) == nullptr);
  CHECK(makeIntArgumentsRun(nullptr, 0, 1) == nullptr);
  CHECK(makeExpression(nullptr, 0) == nullptr);
  CHECK(viewString(nullptr, 0) == nullptr);
  setRLEArgumentFlagOrPropagateTypes(nullptr, 0, 1);
  CHECK(allocateExpressionTree(1, 1, nullptr) == nullptr);
  CHECK(allocateExpressionTree(1, 1, [](size_t /*size*/) -> void* { return nullptr; }) == nullptr);
  freeExpressionTree(nullptr, nullptr);
  CHECK(storeString(nullptr, "text", std::realloc) == 0);
  auto* root = allocateExpressionTree(1, 1, std::malloc);
  REQUIRE(root != nullptr);
  CHECK(storeString(&root, nullptr, std::realloc) == 0);
  auto* const rootBeforeFailedGrowth = root;
  CHECK(storeString(&root, "text",
                    [](void* /*memory*/, size_t /*size*/) -> void* { return nullptr; }) == 0);
  CHECK(root == rootBeforeFailedGrowth);
  freeExpressionTree(root, std::free);
}
