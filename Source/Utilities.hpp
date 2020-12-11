#pragma once
#include "BOSS.hpp"
#include "Expression.hpp"

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
