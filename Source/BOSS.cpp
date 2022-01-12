#include "BOSS.hpp"
#include "BootstrapEngine.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include "Utilities.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <variant>
using namespace boss::utilities;
extern "C" {

BOSSExpression* BOSSEvaluate(BOSSExpression const* arg) {
  try {
    static boss::BootstrapEngine engine;
    return new BOSSExpression{engine.evaluate(arg->delegate.clone())};
  } catch(std::exception const& e) {
    return new BOSSExpression{"ErrorWhenEvaluatingExpression"_(arg->delegate, e.what())};
  }
};
BOSSExpression* longToNewBOSSExpression(long i) { return new BOSSExpression{boss::Expression(i)}; }
BOSSExpression* doubleToNewBOSSExpression(double i) {
  return new BOSSExpression{boss::Expression(i)};
}
BOSSExpression* stringToNewBOSSExpression(char const* i) {
  return new BOSSExpression{boss::Expression(std::string(i))};
}
BOSSExpression* bossSymbolNameToNewBOSSExpression(char const* i) {
  return new BOSSExpression{boss::Expression(boss::Symbol(i))};
}

BOSSSymbol* symbolNameToNewBOSSSymbol(char const* i) { return new BOSSSymbol{boss::Symbol(i)}; }

BOSSExpression* newComplexBOSSExpression(BOSSSymbol* head, size_t cardinality,
                                         BOSSExpression* arguments[]) {
  auto args = boss::ExpressionArguments();
  std::transform(arguments, arguments + cardinality, std::back_insert_iterator(args),
                 [](auto const* a) { return a->delegate.clone(); });
  return new BOSSExpression{boss::ComplexExpression(head->delegate, std::move(args))};
}

char const* bossSymbolToNewString(BOSSSymbol const* arg) {
  auto result = std::stringstream();
  result << arg->delegate;
  return strdup(result.str().c_str());
}

/**
 *     bool = 0, long = 1, double = 2 , std::string = 3, Symbol = 4 , ComplexExpression = 5
 */
size_t getBOSSExpressionTypeID(BOSSExpression const* arg) {
  static_assert(std::is_same_v<bool, std::variant_alternative_t<0, boss::Expression::SuperType>>);
  static_assert(
      std::is_same_v<std::int64_t, std::variant_alternative_t<1, boss::Expression::SuperType>>);
  static_assert(
      std::is_same_v<std::double_t, std::variant_alternative_t<2, boss::Expression::SuperType>>);
  static_assert(
      std::is_same_v<std::string, std::variant_alternative_t<3, boss::Expression::SuperType>>);
  static_assert(
      std::is_same_v<boss::Symbol, std::variant_alternative_t<4, boss::Expression::SuperType>>);
  static_assert(
      std::is_same_v<boss::ComplexExpression,
                     std::variant_alternative_t<5, boss::Expression::SuperType>>); // NOLINT
  return arg->delegate.index();
}

bool getBoolValueFromBOSSExpression(BOSSExpression const* arg) {
  return boss::get<bool>(arg->delegate);
}
std::int64_t getLongValueFromBOSSExpression(BOSSExpression const* arg) {
  return boss::get<std::int64_t>(arg->delegate);
}
std::double_t getDoubleValueFromBOSSExpression(BOSSExpression const* arg) {
  return boss::get<std::double_t>(arg->delegate);
}
char* getNewStringValueFromBOSSExpression(BOSSExpression const* arg) {
  return strdup(boss::get<std::string>(arg->delegate).c_str());
}
char const* getNewSymbolNameFromBOSSExpression(BOSSExpression const* arg) {
  return strdup(boss::get<boss::Symbol>(arg->delegate).getName().c_str());
}

BOSSSymbol* getHeadFromBOSSExpression(BOSSExpression const* arg) {
  return new BOSSSymbol{boss::get<boss::ComplexExpression>(arg->delegate).getHead()};
}
size_t getArgumentCountFromBOSSExpression(BOSSExpression const* arg) {
  return boss::get<boss::ComplexExpression>(arg->delegate).getArguments().size();
}
BOSSExpression** getArgumentsFromBOSSExpression(BOSSExpression const* arg) {
  auto const& args = boss::get<boss::ComplexExpression>(arg->delegate).getArguments();
  auto* result = new BOSSExpression*[args.size() + 1];
  std::transform(begin(args), end(args), result,
                 [](auto const& arg) { return new BOSSExpression{arg.clone()}; });
  result[args.size()] = nullptr;
  return result;
}

void freeBOSSExpression(BOSSExpression* e) {
  delete e; // NOLINT
}
void freeBOSSArguments(BOSSExpression** e) {
  for(auto i = 0U; e[i] != nullptr; i++) {
    delete e[i];
  }
  delete[] e; // NOLINT
}
void freeBOSSSymbol(BOSSSymbol* s) {
  delete s; // NOLINT
}
void freeBOSSString(char* s) {
  std::free(reinterpret_cast<void*>(s)); // NOLINT
}
}
