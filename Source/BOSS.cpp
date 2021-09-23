#include "BOSS.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <ostream>
#include <sstream>
extern "C" {
struct Expression {
  boss::Expression delegate;
};
struct Symbol {
  boss::Symbol delegate;
};
Expression* evaluate(Expression const* arg) {
  static boss::engines::wolfram::Engine engine;
  return new Expression{.delegate = engine.evaluate(arg->delegate)};
};
Expression* intToNewExpression(int i) { return new Expression{.delegate = boss::Expression(i)}; }
Expression* floatToNewExpression(float i) {
  return new Expression{.delegate = boss::Expression(i)};
}
Expression* stringToNewExpression(char const* i) {
  return new Expression{.delegate = boss::Expression(i)};
}
Expression* symbolNameToNewExpression(char* i) {
  return new Expression{.delegate = boss::Expression(boss::Symbol(i))};
}

Symbol* symbolNameToNewSymbol(char* i) { return new Symbol{.delegate = boss::Symbol(i)}; }

Expression* newComplexExpression(Symbol* head, size_t cardinality, Expression* arguments[]) {
  std::vector<boss::Expression> args;
  std::transform(arguments, arguments + cardinality, std::back_insert_iterator(args),
                 [](auto const* a) { return a->delegate; });
  return new Expression{.delegate = boss::ComplexExpression(head->delegate, args)};
}

char const* symbolToNewString(Symbol const* arg) {
  std::stringstream result;
  result << arg->delegate;
  auto* res = strdup(result.str().c_str());
  return res;
}
char const* toString(Expression const* arg) {
  std::stringstream result;
  result << arg->delegate;
  auto* res = strdup(result.str().c_str());
  return res;
}

/**
 *     bool = 0, int = 1, float = 2 , std::string = 3, Symbol = 4 , ComplexExpression = 5
 */
size_t getBOSSTypeID(Expression const* arg) {
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

bool getBoolValueFromExpression(Expression const* arg) { return std::get<bool>(arg->delegate); }
int getIntValueFromExpression(Expression const* arg) { return std::get<int>(arg->delegate); }
float getFloatValueFromExpression(Expression const* arg) { return std::get<float>(arg->delegate); }
char const* getNewStringValueFromExpression(Expression const* arg) {
  return strdup(std::get<std::string>(arg->delegate).c_str());
}
char const* getNewSymbolNameFromExpression(Expression const* arg) {
  return strdup(std::get<boss::Symbol>(arg->delegate).getName().c_str());
}

Symbol* getHeadFromExpression(Expression const* arg) {
  return new Symbol{.delegate = std::get<boss::ComplexExpression>(arg->delegate).getHead()};
}
size_t getArgumentCountFromExpression(Expression const* arg) {
  return std::get<boss::ComplexExpression>(arg->delegate).getArguments().size();
}
Expression** getArgumentsFromExpression(Expression const* arg) {
  auto args = std::get<boss::ComplexExpression>(arg->delegate).getArguments();
  auto* result = new Expression*[args.size()];
  std::transform(begin(args), end(args), result,
                 [](auto const& arg) { return new Expression{.delegate = arg}; });
  return result;
}
}
