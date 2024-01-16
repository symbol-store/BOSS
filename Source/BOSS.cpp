#include "BOSS.hpp"
#include "BootstrapEngine.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include "Serialization.hpp"
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
using boss::expressions::CloneReason;
using std::get; // NOLINT(misc-unused-using-decls)
                // this is required to prevent clang-warnings for get<...>(Expression).
                // I (Holger) suspect this is a compiler-bug

extern "C" {

BOSSExpression* BOSSEvaluate(BOSSExpression* arg) {
  try {
    static boss::engines::BootstrapEngine engine;
    auto* output = new BOSSExpression{engine.evaluate(std::move(arg->delegate))};
    freeBOSSExpression(arg);
    return output;
  } catch(::std::exception const& e) {
    auto args = boss::ExpressionArguments();
    args.emplace_back(std::move(arg->delegate));
    args.emplace_back(std::string{e.what()});
    return new BOSSExpression{
        boss::ComplexExpression("ErrorWhenEvaluatingExpression"_, std::move(args))};
  }
};
BOSSExpression* boolToNewBOSSExpression(bool value) {
  return new BOSSExpression{boss::Expression(value)};
}
BOSSExpression* charToNewBOSSExpression(int8_t value) {
  return new BOSSExpression{boss::Expression(value)};
}
BOSSExpression* intToNewBOSSExpression(int32_t value) {
  return new BOSSExpression{boss::Expression(value)};
}
BOSSExpression* longToNewBOSSExpression(int64_t value) {
  return new BOSSExpression{boss::Expression(value)};
}
BOSSExpression* floatToNewBOSSExpression(float value) {
  return new BOSSExpression{boss::Expression(value)};
}
BOSSExpression* doubleToNewBOSSExpression(double value) {
  return new BOSSExpression{boss::Expression(value)};
}
BOSSExpression* stringToNewBOSSExpression(char const* string) {
  return new BOSSExpression{boss::Expression(::std::string(string))};
}
BOSSExpression* bossSymbolNameToNewBOSSExpression(char const* name) {
  return new BOSSExpression{boss::Expression(boss::Symbol(name))};
}

BOSSSymbol* symbolNameToNewBOSSSymbol(char const* name) {
  return new BOSSSymbol{boss::Symbol(name)};
}

BOSSExpression* newComplexBOSSExpression(BOSSSymbol* head, size_t cardinality,
                                         BOSSExpression* arguments[]) {
  auto args = boss::ExpressionArguments();
  ::std::transform(arguments, arguments + cardinality, ::std::back_insert_iterator(args),
                   [](auto const* a) {
                     return a->delegate.clone(CloneReason::CONVERSION_TO_C_BOSS_EXPRESSION);
                   });
  return new BOSSExpression{boss::ComplexExpression(head->delegate, ::std::move(args))};
}

char const* bossSymbolToNewString(BOSSSymbol const* arg) {
  auto result = ::std::stringstream();
  result << arg->delegate;
  return strdup(result.str().c_str());
}

/**
 *  bool = 0, char = 1, int = 2, long = 3, float = 4, double = 5, std::string = 6, Symbol = 7,
 *  ComplexExpression = 8
 */

size_t getBOSSExpressionTypeID(BOSSExpression const* arg) {
  static_assert(
      ::std::is_same_v<bool,
                       ::std::variant_alternative_t<0, boss::Expression::SuperType>>); // NOLINT
  static_assert(
      ::std::is_same_v<::std::int8_t,
                       ::std::variant_alternative_t<1, boss::Expression::SuperType>>); // NOLINT
  static_assert(
      ::std::is_same_v<::std::int32_t,
                       ::std::variant_alternative_t<2, boss::Expression::SuperType>>); // NOLINT
  static_assert(
      ::std::is_same_v<::std::int64_t,
                       ::std::variant_alternative_t<3, boss::Expression::SuperType>>); // NOLINT
  static_assert(
      ::std::is_same_v<::std::float_t,
                       ::std::variant_alternative_t<4, boss::Expression::SuperType>>); // NOLINT
  static_assert(
      ::std::is_same_v<::std::double_t,
                       ::std::variant_alternative_t<5, boss::Expression::SuperType>>); // NOLINT
  static_assert(
      ::std::is_same_v<::std::string,
                       ::std::variant_alternative_t<6, boss::Expression::SuperType>>); // NOLINT
  static_assert(
      ::std::is_same_v<boss::Symbol,
                       ::std::variant_alternative_t<7, boss::Expression::SuperType>>); // NOLINT
  static_assert(
      ::std::is_same_v<boss::ComplexExpression,
                       ::std::variant_alternative_t<8, boss::Expression::SuperType>>); // NOLINT
  return arg->delegate.index();
}

bool getBoolValueFromBOSSExpression(BOSSExpression const* arg) { return get<bool>(arg->delegate); }
std::int8_t getCharValueFromBOSSExpression(BOSSExpression const* arg) {
  return get<::std::int8_t>(arg->delegate);
}
std::int32_t getIntValueFromBOSSExpression(BOSSExpression const* arg) {
  return get<::std::int32_t>(arg->delegate);
}
std::int64_t getLongValueFromBOSSExpression(BOSSExpression const* arg) {
  return get<::std::int64_t>(arg->delegate);
}
std::float_t getFloatValueFromBOSSExpression(BOSSExpression const* arg) {
  return get<::std::float_t>(arg->delegate);
}
std::double_t getDoubleValueFromBOSSExpression(BOSSExpression const* arg) {
  return get<::std::double_t>(arg->delegate);
}
char* getNewStringValueFromBOSSExpression(BOSSExpression const* arg) {
  return strdup(get<::std::string>(arg->delegate).c_str());
}
char const* getNewSymbolNameFromBOSSExpression(BOSSExpression const* arg) {
  return strdup(get<boss::Symbol>(arg->delegate).getName().c_str());
}

BOSSSymbol* getHeadFromBOSSExpression(BOSSExpression const* arg) {
  return new BOSSSymbol{get<boss::ComplexExpression>(arg->delegate).getHead()};
}
size_t getArgumentCountFromBOSSExpression(BOSSExpression const* arg) {
  return get<boss::ComplexExpression>(arg->delegate).getArguments().size();
}
BOSSExpression** getArgumentsFromBOSSExpression(BOSSExpression const* arg) {
  auto const& args = get<boss::ComplexExpression>(arg->delegate).getArguments();
  auto* result = new BOSSExpression*[args.size() + 1];
  ::std::transform(begin(args), end(args), result, [](auto const& arg) {
    return new BOSSExpression{arg.clone(CloneReason::CONVERSION_TO_C_BOSS_EXPRESSION)};
  });
  result[args.size()] = nullptr;
  return result;
}

void freeBOSSExpression(BOSSExpression* expression) {
  delete expression; // NOLINT
}
void freeBOSSArguments(BOSSExpression** arguments) {
  for(auto i = 0U; arguments[i] != nullptr; i++) {
    delete arguments[i];
  }
  delete[] arguments; // NOLINT
}
void freeBOSSSymbol(BOSSSymbol* symbol) {
  delete symbol; // NOLINT
}
void freeBOSSString(char* string) {
  ::std::free(reinterpret_cast<void*>(string)); // NOLINT
}
}

namespace boss {
Expression evaluate(Expression&& expr) {
  auto* e = new BOSSExpression{std::move(expr)};
  auto* result = BOSSEvaluate(e);
  auto output = ::std::move(result->delegate);
  freeBOSSExpression(result);
  return output;
}
} // namespace boss

extern "C" {

struct PortableBOSSRootExpression* serializeBOSSExpression(struct BOSSExpression* e) {
  return boss::serialization::SerializedExpression(std::move(e->delegate)).extractRoot();
}
struct BOSSExpression* deserializeBOSSExpression(struct PortableBOSSRootExpression* root) {
  return new BOSSExpression{boss::serialization::SerializedExpression(root).deserialize()};
}
}
