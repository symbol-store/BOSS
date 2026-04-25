#pragma once
#include "Algorithm.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boss::utilities {
template <typename ExpressionSystem = DefaultExpressionSystem> class ExtensibleExpressionBuilder {
  Symbol const s;

public:
  explicit ExtensibleExpressionBuilder(Symbol const& s) : s(s) {};
  explicit ExtensibleExpressionBuilder(const ::std::string& s) : s(Symbol(s)) {};
  /**
   * This thing is a bit hacky: when construction Expression, some standard
   * libraries convert char const* to int or bool, not to ::std::string -- so I do
   * it explicitly
   */
  typename ExpressionSystem::Expression
  convertConstCharToStringAndOnToExpression(char const* v) const {
    return ::std::string(v);
  }
  template <typename T>
  typename ExpressionSystem::Expression convertConstCharToStringAndOnToExpression(T&& v) const {
    return std::forward<T>(v);
  }

  template <typename Ts>
  using isAtom = isVariantMember<::std::decay_t<Ts>, typename ExpressionSystem::AtomicExpression>;
  template <typename Ts>
  using isComplexExpression =
      isInstanceOfTemplate<Ts, ExpressionSystem::template ComplexExpressionWithStaticArguments>;
  template <typename Ts>
  using isStaticArgument = ::std::disjunction<isComplexExpression<Ts>, isAtom<Ts>>;
  template <typename T> using isSpanArgument = isInstanceOfTemplate<T, Span>;
  template <typename T>
  using isSpanOfConstArgument = isInstanceOfTemplateWithConstArguments<T, Span>;

  template <typename T>
  using isDynamicArgument =
      ::std::conjunction<::std::negation<isStaticArgument<T>>, ::std::negation<isSpanArgument<T>>>;

  /**
   * build expression from dynamic arguments (or no arguments)
   */
  template <typename... Ts>
  ::std::enable_if_t<sizeof...(Ts) == 0 || std::disjunction_v<isDynamicArgument<Ts>...>,
                     typename ExpressionSystem::ComplexExpression>
  operator()(Ts&&... args /*a*/) const {
    typename ExpressionSystem::ExpressionArguments argList;
    argList.reserve(sizeof...(Ts));
    (argList.push_back(convertConstCharToStringAndOnToExpression(
         ::std::forward<decltype(args)>(args))), // NOLINT(hicpp-no-array-decay)
     ...);
    return (*this)(::std::move(argList));
  }

  /**
   * build expression from dynamic arguments (or no arguments)
   */
  typename ExpressionSystem::ComplexExpression
  operator()(typename ExpressionSystem::ExpressionArguments&& argList /*a*/) const {
    return {s, {}, ::std::move(argList)};
  }

  /**
   * build expression from static arguments, some of which are expressions themselves (passing
   * arguments by rvalue reference)
   */
  template <typename... Ts>
  ::std::enable_if_t<(sizeof...(Ts) > 0) &&
                         !(std::conjunction_v<isSpanArgument<::std::decay_t<Ts>>...>) &&
                         !(std::disjunction_v<isDynamicArgument<Ts>...>),
                     typename ExpressionSystem::template ComplexExpressionWithStaticArguments<
                         ::std::decay_t<Ts>...>>
  operator()(Ts&&... args /*a*/) const {
    return {s, ::std::tuple<::std::decay_t<Ts>...>(::std::forward<Ts>(args)...)};
  };

  /**
   * build expression from span arguments
   */
  template <typename... Ts>
  ::std::enable_if_t<::std::conjunction<isSpanArgument<::std::decay_t<Ts>>...>::value &&
                         (sizeof...(Ts) > 0),
                     typename ExpressionSystem::ComplexExpression>
  operator()(Ts&&... args) const {
    auto spans = std::array {
        std::forward<Ts>(args)...}; // unfortunately, vectors cannot be initialized with move-only
                                    // types which is why we need to put spans into an array first
    return {s, {}, {}, {std::move_iterator(begin(spans)), std::move_iterator(end(spans))}};
  }

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

namespace experimental {
namespace {
class Transformer {

  Expression c;
  char const* expectedHead = nullptr;
  /**
   * by default, transformers are active (i.e., transform if a match is found). However, a
   * transformer deactivates all downstream operators when it matches
   */
  bool isActive = true;
  /**
   * if a transformer is deactivated but the target of a pattern that has been matched, it is still
   * evaluated. The canonical example is a pattern that leads to recursion and some evaluation
   */
  bool isInLineWithMatched = false;

public:
  static constexpr char const* const AnyHead = "FBP[vNqRLj4n11i?p@-4i:!!H_cTcW;";

  Transformer(ComplexExpression&& c, char const* expectedHead, bool isActive = true,
              bool isInLineWithMatched = false)
      : c(std::move(c)), expectedHead(expectedHead), isActive(isActive),
        isInLineWithMatched(isInLineWithMatched) {}
  Transformer(Expression&& c, char const* expectedHead, bool isActive = true,
              bool isInLineWithMatched = false)
      : c(std::move(c)), expectedHead(expectedHead), isActive(isActive),
        isInLineWithMatched(isInLineWithMatched) {}

  template <typename Visitor, typename... StaticArgumentTypes>
  auto process(expressions::ComplexExpressionWithStaticArguments<StaticArgumentTypes...>&& c,
               Visitor visitor) {
    auto [head, statics, dynamics, spans] = std::move(c).decompose();
    if constexpr(std::is_invocable<Visitor, decltype(head), decltype(statics), decltype(dynamics),
                                   decltype(spans)>::value) {
      return (Expression)std::forward<Visitor>(visitor)(std::move(head), std::move(statics),
                                                        std::move(dynamics), std::move(spans));
    } else {
      return (Expression)std::forward<Visitor>(visitor)(std::move(statics), std::move(dynamics),
                                                        std::move(spans));
    }
  }

  template <typename Visitor> Transformer operator>=(Visitor&& visitor) && {
    return std::move(*this) > std::forward<Visitor>(visitor);
  }

  template <typename Visitor> Transformer operator>>=(Visitor&& visitor) && {
    return std::move(*this) >= std::forward<Visitor>(visitor);
  }

  template <typename Visitor> Transformer operator>(Visitor&& visitor) && {
    if(isInLineWithMatched ||
       (isActive && std::holds_alternative<ComplexExpression>(c) &&
        (std::get<ComplexExpression>(c).getHead().getName() == expectedHead ||
         expectedHead == AnyHead))) {
      return {process(std::move(std::get<ComplexExpression>(c)), std::forward<Visitor>(visitor)),
              expectedHead, false, true};
    }
    return std::move(*this);
  }
  template <typename Visitor> Transformer operator>>(Visitor&& visitor) && {
    return std::move(*this) > std::forward<Visitor>(visitor);
  }

  Transformer operator<(char const* expectedHead) && {
    return Transformer(std::move(c), expectedHead, isActive, false);
  }
  Transformer operator<<(char const* expectedHead) && { return std::move(*this) < expectedHead; }

  /*implicit*/ operator Expression() { // NOLINT(hicpp-explicit-conversions)
    return std::visit([](auto&& x) -> Expression { return std::forward<decltype(x)>(x); },
                      std::move(c));
  }
};

inline Transformer operator<(ComplexExpression&& e, char const* expectedHead) {
  return Transformer(std::move(e), expectedHead);
}
inline Transformer operator<<(ComplexExpression&& e, char const* expectedHead) {
  return std::move(e) < expectedHead;
}

inline Transformer operator<(Expression&& e, char const* expectedHead) {
  return Transformer(std::move(e), expectedHead);
}

inline Transformer operator<<(Expression&& e, char const* expectedHead) {
  return std::move(e) < expectedHead;
}

template <typename EvaluateFunctionType> struct Recurse {
  EvaluateFunctionType& evaluate;
  explicit Recurse(EvaluateFunctionType& evaluate) : evaluate(evaluate) {}

  template <typename StaticArgumentTuple, typename DynamicArgumentContainer,
            typename SpanArgumentContainer>
  Expression operator()(Symbol&& head, StaticArgumentTuple&& statics,
                        DynamicArgumentContainer&& dynamics, SpanArgumentContainer&& spans) {
    boss::algorithm::visitTransform(
        dynamics, [this](auto&& arg) { return evaluate(std::forward<decltype(arg)>(arg)); });
    return ComplexExpression(std::move(head), std::forward<StaticArgumentTuple>(statics),
                             std::forward<DynamicArgumentContainer>(dynamics),
                             std::forward<SpanArgumentContainer>(spans));
  }
};

} // namespace

} // namespace experimental

} // namespace boss::utilities
