#pragma once
#include "BOSS.hpp"
#include "Expression.hpp"
#include <ostream>

namespace boss::utilities {
class ExpressionBuilder {
  std::string const s;

public:
  explicit ExpressionBuilder(const std::string& s) : s(s){};
  template <typename... Ts> Expression operator()(Ts... args /*a*/) const {
    return Expression{s, {args...}};
  };
  friend Expression operator|(Expression const& expression, ExpressionBuilder const& builder) {
    return builder(expression);
  };
  operator Expression::Symbol() const { return Expression::Symbol(s); } // NOLINT
};
static ExpressionBuilder operator""_(const char* name, unsigned long /*unused*/) {
  return ExpressionBuilder(name);
};

template <class... Fs> struct overload : Fs... {
  template <class... Ts> explicit overload(Ts&&... ts) : Fs{std::forward<Ts>(ts)}... {}
  using Fs::operator()...;
};

template <class... Ts> overload(Ts&&...) -> overload<std::remove_reference_t<Ts>...>;

} // namespace boss::utilities

static std::ostream& operator<<(std::ostream& out, boss::Expression::ReturnType const& thing) {
  std::visit(boss::utilities::overload(
                 [&](boss::Expression const& e) {
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
                 [&](int value) { out << value; },
                 [&](boss::Expression::Symbol const& value) { out << value.getName(); },
                 [&](float value) { out << value; },
                 [&](std::string const& value) { out << "\"" << value << "\""; }),
             thing);
  return out;
}
