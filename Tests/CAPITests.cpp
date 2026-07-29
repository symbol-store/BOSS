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
