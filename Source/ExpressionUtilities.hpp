#pragma once
#include "Expression.hpp"
#include <arrow/array.h>
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
  typename ExpressionSystem::Expression
  convertConstCharToStringAndOnToExpression(T const&& v) const {
    using Expression = typename ExpressionSystem::Expression;
    using ComplexExpression = typename ExpressionSystem::ComplexExpression;
    if constexpr(std::is_same_v<std::decay_t<decltype(v)>, char const*>) {
      return Expression(std::string((char const*)v));
    } else if constexpr(std::is_same_v<std::decay_t<decltype(v)>, ComplexExpression> ||
                        std::is_same_v<std::decay_t<decltype(v)>, Expression>) {
      return Expression(v.clone());
    } else {
      return Expression(v);
    }
  }
  template <typename T>
  std::enable_if_t<std::is_rvalue_reference_v<T&&>, typename ExpressionSystem::Expression>
  convertConstCharToStringAndOnToExpression(T&& v) const {
    return typename ExpressionSystem::Expression(std::forward<T>(v));
  }

  template <typename... Ts>
  typename ExpressionSystem::ComplexExpression operator()(Ts&&... args /*a*/) const {
    typename ExpressionSystem::ExpressionArguments argList;
    argList.reserve(sizeof...(Ts));
    (
        [this, &argList](auto&& arg) {
          argList.emplace_back(convertConstCharToStringAndOnToExpression<decltype(arg)>(
              std::forward<decltype(arg)>(arg)));
        }(std::move(args)),
        ...);
    return typename ExpressionSystem::ComplexExpression(s, std::move(argList));
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

namespace nasty {
// the ownership model is unclear -- we really need to fix that
static boss::ComplexExpression
arrowArrayToExpression(std::shared_ptr<arrow::Array> const& arrowPtr) {
  union {
    std::shared_ptr<arrow::Array> const* pointer;
    std::pair<int, int> asInts = {0, 0};
  };

  pointer = &arrowPtr;                                  // NOLINT
  return "ArrowArrayPtr"_(asInts.first, asInts.second); // NOLINT
}
static std::shared_ptr<arrow::Array> reconstructArrowArray(int first, int second) {
  union {
    std::shared_ptr<arrow::Array> const* pointer;
    std::pair<int, int> asInts = {0, 0};
  };

  asInts.first = first;   // NOLINT
  asInts.second = second; // NOLINT
  return *pointer;        // NOLINT
}
} // namespace nasty

} // namespace boss::utilities

static std::ostream& operator<<(std::ostream& out, boss::Symbol const& thing) {
  return out << thing.getName();
}
static std::ostream& operator<<(std::ostream& out, boss::ComplexExpression const& e);
static std::ostream& operator<<(std::ostream& out, boss::Expression const& thing) {
  std::visit(
      boss::utilities::overload([&](std::string const& value) { out << "\"" << value << "\""; },
                                [&](bool value) { out << (value ? "True" : "False"); },
                                [&](auto const& value) { out << value; }),
      thing);
  return out;
}
// a specialization for complex expressions is needed, or otherwise
// the complex expression and all its arguments has to be copied to be converted to an Expression
static std::ostream& operator<<(std::ostream& out, boss::ComplexExpression const& e) {
  out << e.getHead() << "[";
  if(!e.getArguments().empty()) {
    out << e.getArguments().front();
    for(auto it = next(e.getArguments().begin()); it != e.getArguments().end(); ++it) {
      out << "," << *it;
    }
  }
  out << "]";
  return out;
}
