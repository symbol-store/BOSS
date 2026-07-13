#include "BOSS.hpp"
#include "BootstrapEngine.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include "PortableBOSSSerialization.h"
#include "Serialization.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iterator>
#include <sstream>
#include <string.h> //NOLINT(hicpp-deprecated-headers) // for strdup
#include <type_traits>
#include <utility>
#include <variant>
using namespace boss::utilities;
using boss::expressions::CloneReason;
using std::get; // NOLINT(misc-unused-using-decls)
                // this is required to prevent clang-warnings for get<...>(Expression).
                // I (Holger) suspect this is a compiler-bug

namespace {
template <typename Element> BOSSExpressionSpan* newBOSSSpan(::std::vector<Element>&& values) {
  return new BOSSExpressionSpan {
      boss::expressions::ExpressionSpanArgument(boss::Span<Element>(::std::move(values)))};
}
template <typename Element> BOSSExpressionSpan* newBOSSSpan(Element const* data, size_t size) {
  return newBOSSSpan(::std::vector<Element>(data, data + size));
}
template <typename Element>
BOSSExpressionSpan* newBOSSSpanFromCStrings(char const* const* data, size_t size) {
  auto values = ::std::vector<Element>();
  values.reserve(size);
  for(size_t index = 0; index < size; ++index) {
    values.emplace_back(data[index]);
  }
  return newBOSSSpan(::std::move(values));
}
template <typename Arguments>
BOSSExpression** cloneToNewBOSSExpressionArray(Arguments const& arguments) {
  auto* result = new BOSSExpression*[arguments.size() + 1];
  ::std::transform(begin(arguments), end(arguments), result, [](auto const& argument) {
    return new BOSSExpression {argument.clone(CloneReason::CONVERSION_TO_C_BOSS_EXPRESSION)};
  });
  result[arguments.size()] = nullptr;
  return result;
}
} // namespace

extern "C" {
/**
 * (destructively) evaluate the expression. The function takes ownership of the expression and is
 * free to break to the object. Any pointers and references to the expression must be considered
 * invalid after calling BOSSEvaluate.
 */
BOSSExpression* BOSSEvaluate(BOSSExpression* arg) {
  try {
    static boss::engines::BootstrapEngine engine;
    auto* output = new BOSSExpression {engine.evaluate(std::move(arg->delegate))};
    freeBOSSExpression(arg);
    return output;
  } catch(::std::exception const& e) {
    auto args = boss::ExpressionArguments();
    args.emplace_back(std::move(arg->delegate));
    args.emplace_back(std::string {e.what()});
    return new BOSSExpression {
        boss::ComplexExpression("ErrorWhenEvaluatingExpression"_, std::move(args))};
  }
};
BOSSExpression* boolToNewBOSSExpression(bool value) {
  return new BOSSExpression {boss::Expression(value)};
}
BOSSExpression* charToNewBOSSExpression(int8_t value) {
  return new BOSSExpression {boss::Expression(value)};
}
BOSSExpression* intToNewBOSSExpression(int32_t value) {
  return new BOSSExpression {boss::Expression(value)};
}
BOSSExpression* longToNewBOSSExpression(int64_t value) {
  return new BOSSExpression {boss::Expression(value)};
}
BOSSExpression* floatToNewBOSSExpression(float value) {
  return new BOSSExpression {boss::Expression(value)};
}
BOSSExpression* doubleToNewBOSSExpression(double value) {
  return new BOSSExpression {boss::Expression(value)};
}
BOSSExpression* stringToNewBOSSExpression(char const* string) {
  return new BOSSExpression {boss::Expression(::std::string(string))};
}
BOSSExpression* symbolNameToNewBOSSExpression(char const* name) {
  return new BOSSExpression {boss::Expression(boss::Symbol(name))};
}

BOSSSymbol* symbolNameToNewBOSSSymbol(char const* name) {
  return new BOSSSymbol {boss::Symbol(name)};
}

BOSSExpression* newComplexBOSSExpression(BOSSSymbol* head, size_t cardinality,
                                         BOSSExpression* arguments[]) {
  auto args = boss::ExpressionArguments();
  ::std::transform(arguments, arguments + cardinality, ::std::back_insert_iterator(args),
                   [](auto const* a) {
                     return a->delegate.clone(CloneReason::CONVERSION_TO_C_BOSS_EXPRESSION);
                   });
  return new BOSSExpression {boss::ComplexExpression(head->delegate, ::std::move(args))};
}

BOSSExpressionSpan* makeBoolBOSSSpan(bool const* data, size_t size) {
  return newBOSSSpan(data, size);
}
BOSSExpressionSpan* makeInt8BOSSSpan(int8_t const* data, size_t size) {
  return newBOSSSpan(data, size);
}
BOSSExpressionSpan* makeInt32BOSSSpan(int32_t const* data, size_t size) {
  return newBOSSSpan(data, size);
}
BOSSExpressionSpan* makeInt64BOSSSpan(int64_t const* data, size_t size) {
  return newBOSSSpan(data, size);
}
BOSSExpressionSpan* makeFloatBOSSSpan(float const* data, size_t size) {
  return newBOSSSpan(data, size);
}
BOSSExpressionSpan* makeDoubleBOSSSpan(double const* data, size_t size) {
  return newBOSSSpan(data, size);
}
BOSSExpressionSpan* makeStringBOSSSpan(char const* const* data, size_t size) {
  return newBOSSSpanFromCStrings<::std::string>(data, size);
}
BOSSExpressionSpan* makeSymbolBOSSSpan(char const* const* data, size_t size) {
  return newBOSSSpanFromCStrings<boss::Symbol>(data, size);
}

size_t getBOSSSpanBeginAddress(BOSSExpressionSpan const* span) {
  return ::std::visit(
      [](auto const& typedSpan) -> size_t {
        using IteratorType = decltype(typedSpan.begin());
        if constexpr(::std::is_pointer_v<IteratorType>) {
          return reinterpret_cast<size_t>(typedSpan.begin());
        } else {
          return 0;
        }
      },
      span->delegate);
}

BOSSExpression* newComplexBOSSExpressionWithSpans(BOSSSymbol* head, size_t cardinality,
                                                  BOSSExpression* arguments[], size_t spanCount,
                                                  BOSSExpressionSpan* spans[]) {
  auto args = boss::ExpressionArguments();
  ::std::transform(arguments, arguments + cardinality, ::std::back_insert_iterator(args),
                   [](auto const* a) {
                     return a->delegate.clone(CloneReason::CONVERSION_TO_C_BOSS_EXPRESSION);
                   });
  auto spanArguments = boss::expressions::ExpressionSpanArguments();
  spanArguments.reserve(spanCount);
  for(size_t index = 0; index < spanCount; ++index) {
    spanArguments.emplace_back(::std::move(spans[index]->delegate));
  }
  return new BOSSExpression {
      boss::ComplexExpression(head->delegate, {}, ::std::move(args), ::std::move(spanArguments))};
}

size_t getDynamicArgumentCountFromBOSSExpression(BOSSExpression const* arg) {
  return get<boss::ComplexExpression>(arg->delegate).getDynamicArguments().size();
}
BOSSExpression** getDynamicArgumentsFromBOSSExpression(BOSSExpression const* arg) {
  return cloneToNewBOSSExpressionArray(
      get<boss::ComplexExpression>(arg->delegate).getDynamicArguments());
}

size_t getSpanArgumentCountFromBOSSExpression(BOSSExpression const* arg) {
  return get<boss::ComplexExpression>(arg->delegate).getSpanArguments().size();
}
BOSSExpressionSpan** getSpanArgumentsFromBOSSExpression(BOSSExpression* arg) {
  auto spanArguments = ::std::move(get<boss::ComplexExpression>(arg->delegate)).getSpanArguments();
  auto* result = new BOSSExpressionSpan*[spanArguments.size() + 1];
  ::std::transform(::std::make_move_iterator(begin(spanArguments)),
                   ::std::make_move_iterator(end(spanArguments)), result,
                   [](auto&& span) { return new BOSSExpressionSpan {::std::move(span)}; });
  result[spanArguments.size()] = nullptr;
  return result;
}
void freeBOSSSpanArray(BOSSExpressionSpan** array) {
  delete[] array; // NOLINT
}
void freeBOSSExpressionSpan(BOSSExpressionSpan* span) {
  delete span; // NOLINT
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
  return new BOSSSymbol {get<boss::ComplexExpression>(arg->delegate).getHead()};
}
size_t getArgumentCountFromBOSSExpression(BOSSExpression const* arg) {
  return get<boss::ComplexExpression>(arg->delegate).getArguments().size();
}
BOSSExpression** getArgumentsFromBOSSExpression(BOSSExpression const* arg) {
  return cloneToNewBOSSExpressionArray(get<boss::ComplexExpression>(arg->delegate).getArguments());
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
  auto* e = new BOSSExpression {std::move(expr)};
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
  return new BOSSExpression {boss::serialization::SerializedExpression(root).deserialize()};
}
}
