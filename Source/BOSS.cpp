#include "BOSS.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <iterator>
#include <ostream>
#include <sstream>
extern "C" {
struct BOSSExpression {
  boss::Expression delegate;
};
struct BOSSSymbol {
  boss::Symbol delegate;
};
BOSSExpression* BOSSEvaluate(BOSSExpression const* arg) {
  static boss::engines::wolfram::Engine engine;
  return new BOSSExpression{.delegate = engine.evaluate(arg->delegate)};
};
BOSSExpression* intToNewBOSSExpression(int i) {
  return new BOSSExpression{.delegate = boss::Expression(i)};
}
BOSSExpression* floatToNewBOSSExpression(float i) {
  return new BOSSExpression{.delegate = boss::Expression(i)};
}
BOSSExpression* stringToNewBOSSExpression(char const* i) {
  return new BOSSExpression{.delegate = boss::Expression(i)};
}
BOSSExpression* bossSymbolNameToNewBOSSExpression(char const* i) {
  return new BOSSExpression{.delegate = boss::Expression(boss::Symbol(i))};
}

BOSSSymbol* symbolNameToNewBOSSSymbol(char const* i) {
  return new BOSSSymbol{.delegate = boss::Symbol(i)};
}

BOSSExpression* newComplexBOSSExpression(BOSSSymbol* head, size_t cardinality,
                                         BOSSExpression* arguments[]) {
  auto args = std::vector<boss::Expression>();
  std::transform(arguments, arguments + cardinality, std::back_insert_iterator(args),
                 [](auto const* a) { return a->delegate; });
  return new BOSSExpression{.delegate = boss::ComplexExpression(head->delegate, args)};
}

char const* bossSymbolToNewString(BOSSSymbol const* arg) {
  auto result = std::stringstream();
  result << arg->delegate;
  return strdup(result.str().c_str());
}

/**
 *     bool = 0, int = 1, float = 2 , std::string = 3, Symbol = 4 , ComplexExpression = 5
 */
size_t getBOSSExpressionTypeID(BOSSExpression const* arg) {
  static_assert(
      std::is_same_v<
          bool, std::variant_alternative_t<0, boss::DefaultExpressionSystem::AtomicExpression>>);
  static_assert(
      std::is_same_v<
          int, std::variant_alternative_t<1, boss::DefaultExpressionSystem::AtomicExpression>>);
  static_assert(
      std::is_same_v<
          float, std::variant_alternative_t<2, boss::DefaultExpressionSystem::AtomicExpression>>);
  static_assert(std::is_same_v<
                std::string,
                std::variant_alternative_t<3, boss::DefaultExpressionSystem::AtomicExpression>>);
  static_assert(std::is_same_v<
                boss::Symbol,
                std::variant_alternative_t<4, boss::DefaultExpressionSystem::AtomicExpression>>);
  return arg->delegate.index();
}

bool getBoolValueFromBOSSExpression(BOSSExpression const* arg) {
  return std::get<bool>(arg->delegate);
}
int getIntValueFromBOSSExpression(BOSSExpression const* arg) {
  return std::get<int>(arg->delegate);
}
float getFloatValueFromBOSSExpression(BOSSExpression const* arg) {
  return std::get<float>(arg->delegate);
}
char const* getNewStringValueFromBOSSExpression(BOSSExpression const* arg) {
  return strdup(std::get<std::string>(arg->delegate).c_str());
}
char const* getNewSymbolNameFromBOSSExpression(BOSSExpression const* arg) {
  return strdup(std::get<boss::Symbol>(arg->delegate).getName().c_str());
}

BOSSSymbol* getHeadFromBOSSExpression(BOSSExpression const* arg) {
  return new BOSSSymbol{.delegate = std::get<boss::ComplexExpression>(arg->delegate).getHead()};
}
size_t getArgumentCountFromBOSSExpression(BOSSExpression const* arg) {
  return std::get<boss::ComplexExpression>(arg->delegate).getArguments().size();
}
BOSSExpression** getArgumentsFromBOSSExpression(BOSSExpression const* arg) {
  auto args = std::get<boss::ComplexExpression>(arg->delegate).getArguments();
  auto* result = new BOSSExpression*[args.size()];
  std::transform(begin(args), end(args), result,
                 [](auto const& arg) { return new BOSSExpression{.delegate = arg}; });
  return result;
}

void freeBOSSExpression(BOSSExpression* e) {
  delete e; // NOLINT
}
void freeBOSSSymbol(BOSSSymbol* s) {
  delete s; // NOLINT
}
}
