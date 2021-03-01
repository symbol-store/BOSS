#pragma once
#include "Expression.hpp"
#include <ostream>

namespace boss::utilities {
class ExpressionBuilder {
  Symbol const s;

public:
  explicit ExpressionBuilder(Symbol const& s) : s(s){};
  explicit ExpressionBuilder(const std::string& s) : s(Symbol(s)){};
  /**
   * This thing is a bit hacky: when construction Expression, some standard
   * libraries convert char const* to int or bool, not to std::string -- so I do
   * it explicitly
   */
  template <typename T>
  Expression convertConstCharToStringAndOnToExpression(
      std::enable_if_t<std::is_same_v<T, char const*>, T> v) const {
    return Expression(std::string(v));
  }
  /**
   * default overload for conversion
   */
  template <typename T>
  Expression convertConstCharToStringAndOnToExpression(
      std::enable_if_t<!std::is_same_v<T, char const*>, T> v) const {
    return Expression(v);
  }

  template <typename... Ts> ComplexExpression operator()(Ts... args /*a*/) const {
    return ComplexExpression{s,
                             {convertConstCharToStringAndOnToExpression<decltype(args)>(args)...}};
  };
  friend Expression operator|(Expression const& expression, ExpressionBuilder const& builder) {
    return builder(expression);
  };
  operator Symbol() const { return Symbol(s); } // NOLINT
};
static ExpressionBuilder operator""_(const char* name, size_t /*unused*/) {
  return ExpressionBuilder(name);
};

template <class... Fs> struct overload : Fs... {
  template <class... Ts> explicit overload(Ts&&... ts) : Fs{std::forward<Ts>(ts)}... {}
  using Fs::operator()...;
};

template <class... Ts> overload(Ts&&...) -> overload<std::remove_reference_t<Ts>...>;

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
                 [&](auto value) { out << value; }),
             thing);
  return out;
}
