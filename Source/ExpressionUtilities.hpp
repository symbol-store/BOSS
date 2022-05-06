#pragma once
#include "Expression.hpp"
#include "Utilities.hpp"
#include <arrow/array.h>
#include <cstdint>
#include <map>
#include <memory>
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
  typename ExpressionSystem::Expression convertConstCharToStringAndOnToExpression(T&& v) const {
    using Expression = typename ExpressionSystem::Expression;
    using ComplexExpression = typename ExpressionSystem::ComplexExpression;
    if constexpr(std::is_same_v<std::decay_t<decltype(v)>, char const*>) {
      return Expression(std::string((char const*)v));
    } else if constexpr(std::is_same_v<std::decay_t<decltype(v)>, ComplexExpression> ||
                        std::is_same_v<std::decay_t<decltype(v)>, Expression>) {
      if constexpr(std::is_rvalue_reference_v<T> && !std::is_const_v<std::remove_reference_t<T>>) {
        return Expression(std::forward<T>(v));
      }
      return Expression(v.clone());
    } else {
      return Expression(v);
    }
  }
  // template <typename T>
  // std::enable_if_t<std::is_rvalue_reference_v<T&&>, typename ExpressionSystem::Expression>
  // convertConstCharToStringAndOnToExpression(T&& v) const {
  //   return typename ExpressionSystem::Expression(std::forward<T>(v));
  // }

  template <typename Ts>
  using isAtom = isVariantMember<std::decay_t<Ts>, typename ExpressionSystem::AtomicExpression>;
  template <typename Ts>
  using isComplexExpression =
      isInstanceOfTemplate<Ts, ExpressionSystem::template ComplexExpressionWithStaticArguments>;
  template <typename Ts>
  using isStaticArgument = std::disjunction<isComplexExpression<Ts>, isAtom<Ts>>;
  template <typename T> using isSpanArgument = isInstanceOfTemplate<T, Span>;

  template <typename T>
  using isDynamicArgument =
      std::conjunction<std::negation<isStaticArgument<T>>, std::negation<isSpanArgument<T>>>;

  /**
   * build expression from dynamic arguments
   */
  template <typename... Ts>
  std::enable_if_t<std::disjunction<isDynamicArgument<Ts>...>::value,
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
   * build expression from span arguments
   */
  template <typename... Ts>
  std::enable_if_t<std::disjunction<isSpanArgument<Ts>...>::value,
                   typename ExpressionSystem::ComplexExpression>
  operator()(Ts&&... args /*a*/) const {
    typename ExpressionSystem::ExpressionSpanArguments argList;
    argList.reserve(sizeof...(Ts));
    ([this, &argList](auto&& arg) { argList.emplace_back(std::forward<decltype(arg)>(arg)); }(
         std::move(args)),
     ...);
    return move(typename ExpressionSystem::ComplexExpression(s, {}, {}, std::move(argList)));
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
static boss::ComplexExpressionWithStaticArguments<std::int64_t>
arrowArrayToExpression(std::shared_ptr<arrow::Array> const& arrowPtr) {
  static_assert(sizeof(void*) == sizeof(std::int64_t),
                "pointers are not 64-bit -- this might break in funky ways");
  return "ArrowArrayPtr"_(reinterpret_cast<std::int64_t>(&arrowPtr));
}
static std::shared_ptr<arrow::Array> reconstructArrowArray(std::int64_t addressAsLong) {
  static_assert(sizeof(void*) == sizeof(std::int64_t),
                "pointers are not 64-bit -- this might break in funky ways");
  return *reinterpret_cast<std::shared_ptr<arrow::Array> const*>(addressAsLong);
}
} // namespace nasty

} // namespace boss::utilities

static std::ostream& operator<<(std::ostream& out, boss::Symbol const& thing) {
  return out << thing.getName();
}
template <typename... StaticArguments, typename... AdditionalAtoms>
static std::ostream&
operator<<(std::ostream& out,
           boss::ComplexExpressionWithAdditionalCustomAtoms<std::tuple<StaticArguments...>,
                                                            AdditionalAtoms...> const& e);

template <bool ConstWrappee, typename... AdditionalCustomAtoms>
::std::ostream&
operator<<(::std::ostream& stream,
           boss::ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...> const& argument);

template <typename... AdditionalAtoms>
static std::ostream&
operator<<(std::ostream& out,
           boss::ExpressionWithAdditionalCustomAtoms<AdditionalAtoms...> const& thing) {
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
template <typename... StaticArguments, typename... AdditionalAtoms>
static std::ostream&
operator<<(std::ostream& out,
           boss::ComplexExpressionWithAdditionalCustomAtoms<std::tuple<StaticArguments...>,
                                                            AdditionalAtoms...> const& e) {
  out << e.getHead() << "[";
  if(!e.getArguments().empty()) {
    out << e.getArguments().front();
    for(auto it = std::next(e.getArguments().begin()); it != e.getArguments().end(); ++it) {
      out << "," << *it;
    }
  }
  out << "]";
  return out;
}

template <typename, typename, bool> struct Wow;

template <bool ConstWrappee, typename... AdditionalCustomAtoms>
::std::ostream&
operator<<(::std::ostream& stream,
           boss::ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...> const& argument) {
  return std::visit(
      [&stream](auto&& argument) -> auto& {
        if constexpr(std::disjunction_v<std::is_same<std::decay_t<decltype(argument)>,
                                                     std::vector<bool>::reference>,
                                        std::is_same<std::decay_t<decltype(argument)>,
                                                     std::vector<bool>::const_reference>>) {
          return stream << (bool)argument;
        } else {
          return stream << argument.get();
        }
      },
      argument.getArgument());
}

namespace boss {
class bad_variant_access : public std::bad_variant_access {
  std::string const whatString;

public:
  explicit bad_variant_access(std::string const& whatString) : whatString(whatString) {}
  const char* what() const noexcept override { return whatString.c_str(); }
};

template <typename T, typename TInput> decltype(auto) get(TInput&& v) {
  try {
    if constexpr(std::is_rvalue_reference_v<decltype(v)>) {
      return std::move(std::get<T>(std::forward<TInput>(v)));
    } else if constexpr(std::is_lvalue_reference_v<decltype(v)>) {
      return std::visit(
          [](auto& unwrapped) -> std::conditional_t<
                                  std::is_const_v<std::remove_reference_t<decltype(v)>> ||
                                      isConstArgumentWrapper<std::remove_reference_t<decltype(v)>>,
                                  T const&, T&> {
            if constexpr(std::is_same_v<std::decay_t<decltype(unwrapped)>, std::decay_t<T>>) {
              return unwrapped;
            } else if constexpr(std::is_same_v<std::decay_t<decltype(unwrapped)>,
                                               std::decay_t<std::reference_wrapper<T>>>) {
              return unwrapped.get();
            } else if constexpr(utilities::isInstanceOfTemplate<
                                    decltype(v),
                                    boss::ExpressionWithAdditionalCustomAtoms>::value) {
              return std::get<T>(unwrapped);
            } else if constexpr(utilities::isInstanceOfTemplate<std::decay_t<decltype(unwrapped)>,
                                                                std::reference_wrapper>::value) {
              if constexpr(utilities::isInstanceOfTemplate<
                               std::decay_t<decltype(unwrapped.get())>,
                               boss::ExpressionWithAdditionalCustomAtoms>::value) {
                return std::get<T>(unwrapped.get());
              }
            }
            throw std::bad_variant_access();
          },
          std::forward<TInput>(v));
    } else {
      return std::visit(
          [](auto&& unwrapped) {
            if constexpr(std::is_same_v<std::decay_t<decltype(unwrapped)>, std::decay_t<T>>) {
              if constexpr(utilities::isInstanceOfTemplate<
                               decltype(unwrapped),
                               boss::ExpressionWithAdditionalCustomAtoms>::value ||
                           utilities::isInstanceOfTemplate<
                               std::decay_t<decltype(unwrapped)>,
                               boss::ComplexExpressionWithAdditionalCustomAtoms>::value) {
                return unwrapped.clone();
              }
              return unwrapped;
            } else if constexpr(utilities::isInstanceOfTemplate<
                                    decltype(v),
                                    boss::ExpressionWithAdditionalCustomAtoms>::value) {
              return std::get<T>(std::forward<TInput>(unwrapped));
            }
            throw std::bad_variant_access();
          },
          std::forward<TInput>(v));
    }
  } catch(std::bad_variant_access const& e) {
    std::stringstream s;
    s << "expected and actual type mismatch in expression \"";
    if(!v.valueless_by_exception()) {
      s << v;
    } else {
      s << "valueless by exception";
    }
    static auto typenames = std::map<std::type_index, char const*>{{typeid(int64_t), "long"},
                                                                   {typeid(Symbol), "Symbol"},
                                                                   {typeid(bool), "bool"},
                                                                   {typeid(double_t), "double"},
                                                                   {typeid(std::string), "string"}};
    s << "\", expected "
      << (typenames.count(typeid(T)) ? typenames.at(typeid(T)) : typeid(T).name());
    throw bad_variant_access(s.str());
  }
};

} // namespace boss
