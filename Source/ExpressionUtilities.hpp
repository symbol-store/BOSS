#pragma once
#include "Expression.hpp"
#include "Utilities.hpp"
#include <arrow/array.h>
#include <map>
#include <ostream>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>

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

  template <typename Ts>
  using isAtom = isVariantMember<std::decay_t<Ts>, typename ExpressionSystem::AtomicExpression>;
  template <typename Ts>
  using isComplexExpression =
      isInstanceOfTemplate<Ts, ExpressionSystem::template ComplexExpressionWithStaticArguments>;
  template <typename Ts>
  using isStaticArgument = std::disjunction<isComplexExpression<Ts>, isAtom<Ts>>;

  /**
   * build expression from dynamic arguments
   */
  template <typename... Ts>
  std::enable_if_t<std::disjunction<std::negation<isStaticArgument<Ts>>...>::value,
                   typename ExpressionSystem::ComplexExpression>
  operator()(Ts&&... args /*a*/) const {
    typename ExpressionSystem::ExpressionArguments argList;
    argList.reserve(sizeof...(Ts));
    (
        [this, &argList](auto&& arg) {
          argList.emplace_back(convertConstCharToStringAndOnToExpression<decltype(arg)>(
              std::forward<decltype(arg)>(arg)));
        }(std::move(args)),
        ...);
    return move(typename ExpressionSystem::ComplexExpression(s, std::move(argList)));
  }

  /**
   * build expression from static arguments, some of which are expressions themselves (passing
   * arguments by rvalue reference)
   */
  template <typename... Ts>
  std::enable_if_t<
      (sizeof...(Ts) > 0) && std::conjunction<isStaticArgument<Ts>...,
                                              std::disjunction<isComplexExpression<Ts>>...>::value,
      typename ExpressionSystem::template ComplexExpressionWithStaticArguments<std::decay_t<Ts>...>>
  operator()(Ts&&... args /*a*/) const {
    return move(
        typename ExpressionSystem::template ComplexExpressionWithStaticArguments<
            std::decay_t<Ts>...>(s, std::tuple<std::decay_t<Ts>...>(std::forward<Ts>(args)...)));
  };

  /**
   * build expression from static arguments, all of which are atoms (passing arguments by value)
   */
  template <typename... Ts>
  std::enable_if_t<
      std::conjunction<isAtom<Ts>...>::value,
      typename ExpressionSystem::template ComplexExpressionWithStaticArguments<std::decay_t<Ts>...>>
  operator()(Ts... args /*a*/) const {
    return move(
        typename ExpressionSystem::template ComplexExpressionWithStaticArguments<
            std::decay_t<Ts>...>(s, std::tuple<std::decay_t<Ts>...>(std::forward<Ts>(args)...)));
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
static boss::ComplexExpressionWithStaticArguments<int, int>
arrowArrayToExpression(std::shared_ptr<arrow::Array> const& arrowPtr) {
  union {
    std::shared_ptr<arrow::Array> const* pointer;
    std::pair<int, int> asInts = {0, 0};
  };

  pointer = &arrowPtr;                    // NOLINT(cppcoreguidelines-pro-type-union-access)
  return "ArrowArrayPtr"_(asInts.first,   // NOLINT(cppcoreguidelines-pro-type-union-access)
                          asInts.second); // NOLINT(cppcoreguidelines-pro-type-union-access)
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
template <typename... StaticArguments>
static std::ostream&
operator<<(std::ostream& out,
           boss::ComplexExpressionWithStaticArguments<StaticArguments...> const& e);
static std::ostream& operator<<(std::ostream& out, boss::Expression const& thing) {
  std::visit(
      boss::utilities::overload([&](std::string const& value) { out << "\"" << value << "\""; },
                                [&](bool value) { out << (value ? "True" : "False"); },
                                [&](auto const& value) { out << value; }),
      thing);
  return out;
}

/**
 * a specialization for complex expressions is needed. Otherwise the complex
 * expression and all its arguments have to be copied to be converted to an
 * Expression
 */
template <typename... StaticArguments>
static std::ostream&
operator<<(std::ostream& out,
           boss::ComplexExpressionWithStaticArguments<StaticArguments...> const& e) {
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

namespace boss {
class bad_variant_access : public std::bad_variant_access {
  std::string const whatString;

public:
  explicit bad_variant_access(std::string const& whatString) : whatString(whatString) {}
  const char* what() const noexcept override { return whatString.c_str(); }
};

template <typename T, typename TInput> auto&& get(TInput&& v) {
  try {
    return std::move(std::get<T>(std::forward<TInput>(v)));
  } catch(std::bad_variant_access const& e) {
    std::stringstream s;
    s << "expected and actual type mismatch in expression \"";
    if(!v.valueless_by_exception()) {
      s << v;
    } else {
      s << "valueless by exception";
    }
    static auto typenames = std::map<std::type_index, char const*>{{typeid(int), "int"},
                                                                   {typeid(Symbol), "Symbol"},
                                                                   {typeid(bool), "bool"},
                                                                   {typeid(float), "float"},
                                                                   {typeid(std::string), "string"}};
    s << "\", expected "
      << (typenames.count(typeid(T)) ? typenames.at(typeid(T)) : typeid(T).name());
    throw bad_variant_access(s.str());
  }
};

} // namespace boss
