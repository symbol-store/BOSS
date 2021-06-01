#pragma once
#include "Expression.hpp"
#include <ostream>

namespace boss::utilities {
template <typename ExpressionSystem = DefaultExpressionSystem> class ExtensibleExpressionBuilder {
  Symbol const s;

public:
  explicit ExtensibleExpressionBuilder(Symbol const& s) : s(s){};
  explicit ExtensibleExpressionBuilder(const std::string& s) : s(Symbol(s)){};
  /**
   * This thing is a bit hacky: when construction Expression, some standard
   * libraries convert char const* to int or bool, not to std::string -- so I do
   * it explicitly
   */
  template <typename T>
  typename ExpressionSystem::Expression convertConstCharToStringAndOnToExpression(
      std::enable_if_t<std::is_same_v<T, char const*>, T> v) const {
    return typename ExpressionSystem::Expression(std::string(v));
  }
  /**
   * default overload for conversion
   */
  template <typename T>
  typename ExpressionSystem::Expression convertConstCharToStringAndOnToExpression(
      std::enable_if_t<!std::is_same_v<T, char const*>, T> v) const {
    return typename ExpressionSystem::Expression(v);
  }

  template <typename... Ts>
  typename ExpressionSystem::ComplexExpression operator()(Ts... args /*a*/) const {
    return typename ExpressionSystem::ComplexExpression{
        s, {convertConstCharToStringAndOnToExpression<decltype(args)>(args)...}};
  };
  friend typename ExpressionSystem::Expression
  operator|(typename ExpressionSystem::Expression const& expression,
            ExtensibleExpressionBuilder const& builder) {
    return builder(expression);
  };
  operator Symbol() const { return Symbol(s); } // NOLINT
};
using ExpressionBuilder = ExtensibleExpressionBuilder<>;
static ExpressionBuilder operator""_(const char* name, size_t /*unused*/) {
  return ExpressionBuilder(name);
};
} // namespace boss::utilities

static std::ostream& operator<<(std::ostream& out, boss::Symbol const& thing) {
  return out << thing.getName();
}
static std::ostream& operator<<(std::ostream& out, boss::Expression const& thing) {
  std::visit(boss::utilities::overload(
                 [&](boss::ComplexExpression const& e) {
                   out << e.getHead() << "[";
                   if(!e.getArguments().empty()) {
                     out << e.getArguments().front();
                     for(auto it = next(e.getArguments().begin()); it != e.getArguments().end();
                         ++it) {
                       out << "," << *it;
                     }
                   }
                   out << "]";
                 },
                 [&](std::string const& value) { out << "\"" << value << "\""; },
                 [&](bool value) { out << (value ? "True" : "False"); },
                 [&](auto value) { out << value; }),
             (boss::Expression::SuperType const&)thing);
  return out;
}
