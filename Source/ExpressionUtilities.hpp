#pragma once
#include "Algorithm.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boss::utilities {

namespace experimental {
namespace sentinel {
inline const Symbol String_ {"<String_FBP[vNqRLj4n11i?p@-4i:!!H_cTcW;"};
inline const Symbol Symbol_ {"<Symbol_FBP[vNqRLj4n11i?p@-4i:!!H_cTcW;"};
inline const Symbol Integer_ {"<Integer_FBP[vNqRLj4n11i?p@-4i:!!H_cTcW;"};
inline const Symbol Any_ {"<Any_FBP[vNqRLj4n11i?p@-4i:!!H_cTcW;"};
inline const Symbol AnySequence_ {"<AnySequence_FBP[vNqRLj4n11i?p@-4i:!!H_cTcW;"};
inline constexpr char const* const AnyHead = "FBP[vNqRLj4n11i?p@-4i:!!H_cTcW;";
} // namespace sentinel
} // namespace experimental

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

inline bool matchArg(Expression const& subject, Expression const& pattern);
inline bool matchArgSequence(ExpressionArguments const& subjects, std::size_t subjectIndex,
                             ExpressionArguments const& patterns, std::size_t patternIndex);

inline bool matchSpanArg(boss::expressions::ExpressionSpanArgument const& subjectSpan,
                         boss::expressions::ExpressionSpanArgument const& patternSpan) {
  return std::visit(
      [&subjectSpan](auto const& patSpan) -> bool {
        using PatternScalar =
            std::remove_const_t<typename std::decay_t<decltype(patSpan)>::element_type>;
        return std::visit(
            [&patSpan](auto const& subjSpan) -> bool {
              using SubjectScalar =
                  std::remove_const_t<typename std::decay_t<decltype(subjSpan)>::element_type>;
              if constexpr(std::is_same_v<PatternScalar, SubjectScalar>) {
                return subjSpan.size() == patSpan.size() &&
                       std::equal(subjSpan.begin(), subjSpan.end(), patSpan.begin());
              }
              return false;
            },
            subjectSpan);
      },
      patternSpan);
}

inline bool matchesPattern(ComplexExpression const& subject, ComplexExpression const& pattern) {
  if(subject.getHead().getName() != pattern.getHead().getName()) {
    return false;
  }
  auto const& subjectSpanArgs = subject.getSpanArguments();
  auto const& patternSpanArgs = pattern.getSpanArguments();
  if(subjectSpanArgs.size() != patternSpanArgs.size()) {
    return false;
  }
  if(!std::equal(subjectSpanArgs.begin(), subjectSpanArgs.end(), patternSpanArgs.begin(),
                 matchSpanArg)) {
    return false;
  }
  auto const& subjectArgs = subject.getDynamicArguments();
  auto const& patternArgs = pattern.getDynamicArguments();
  return matchArgSequence(subjectArgs, 0, patternArgs, 0);
}

inline bool matchArgSequence(ExpressionArguments const& subjects, std::size_t subjectIndex,
                             ExpressionArguments const& patterns, std::size_t patternIndex) {
  if(patternIndex == patterns.size()) {
    return subjectIndex == subjects.size();
  }
  auto const& currentPattern = patterns[patternIndex];
  if(std::holds_alternative<Symbol>(currentPattern) &&
     std::get<Symbol>(currentPattern).getName() == sentinel::AnySequence_.getName()) {
    for(std::size_t consumed = 0; consumed <= subjects.size() - subjectIndex; ++consumed) {
      if(matchArgSequence(subjects, subjectIndex + consumed, patterns, patternIndex + 1)) {
        return true;
      }
    }
    return false;
  }
  if(subjectIndex == subjects.size()) {
    return false;
  }
  return matchArg(subjects[subjectIndex], currentPattern) &&
         matchArgSequence(subjects, subjectIndex + 1, patterns, patternIndex + 1);
}

inline bool matchArg(Expression const& subject, Expression const& pattern) {
  if(std::holds_alternative<Symbol>(pattern)) {
    auto const& sym = std::get<Symbol>(pattern).getName();
    if(sym == sentinel::Any_.getName()) {
      return true;
    }
    if(sym == sentinel::String_.getName()) {
      return std::holds_alternative<std::string>(subject);
    }
    if(sym == sentinel::Symbol_.getName()) {
      return std::holds_alternative<Symbol>(subject);
    }
    if(sym == sentinel::Integer_.getName()) {
      return std::holds_alternative<std::int64_t>(subject) ||
             std::holds_alternative<std::int32_t>(subject) ||
             std::holds_alternative<std::int8_t>(subject);
    }
  }
  if(subject.index() != pattern.index()) {
    return false;
  }
  return std::visit(
      [&subject](auto const& patVal) -> bool {
        using T = std::decay_t<decltype(patVal)>;
        if constexpr(std::is_same_v<T, ComplexExpression>) {
          return matchesPattern(std::get<ComplexExpression>(subject), patVal);
        } else {
          return std::get<T>(subject) == patVal;
        }
      },
      pattern);
}

class Transformer {

  Expression c;
  char const* expectedHead = nullptr;
  /**
   * by default, transformers are active (i.e., transform if a match is found). However, a
   * transformer deactivates all downstream operators when it matches
   */
  bool isActive = true;
  /**
   * if a transformer is deactivated but the target of a pattern that has been matched, it is
   * still evaluated. The canonical example is a pattern that leads to recursion and some
   * evaluation
   */
  bool isInLineWithMatched = false;

public:
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
    bool const shouldFire = isInLineWithMatched || (isActive && expectedHead == sentinel::AnyHead);
    if(!shouldFire) {
      return std::move(*this);
    }
    if(std::holds_alternative<ComplexExpression>(c)) {
      return {process(std::move(std::get<ComplexExpression>(c)), std::forward<Visitor>(visitor)),
              expectedHead, false, true};
    }
    if constexpr(std::is_invocable_v<std::decay_t<Visitor>, Expression&&>) {
      return {
          (Expression)std::forward<Visitor>(visitor)(std::visit(
              [](auto&& x) -> Expression { return std::forward<decltype(x)>(x); }, std::move(c))),
          expectedHead, false, true};
    }
    return std::move(*this);
  }
  template <typename Visitor> Transformer operator>>(Visitor&& visitor) && {
    return std::move(*this) > std::forward<Visitor>(visitor);
  }

  Transformer operator<(char const* stringPattern) && {
    bool const matched = isActive && std::holds_alternative<std::string>(c) &&
                         std::get<std::string>(c) == stringPattern;
    return Transformer(std::move(c), matched ? sentinel::AnyHead : nullptr, isActive, false);
  }
  Transformer operator<<(char const* stringPattern) && { return std::move(*this) < stringPattern; }

  Transformer operator<(ComplexExpression const& pattern) && {
    bool const matched = isActive && std::holds_alternative<ComplexExpression>(c) &&
                         matchesPattern(std::get<ComplexExpression>(c), pattern);
    return Transformer(std::move(c), matched ? sentinel::AnyHead : nullptr, isActive, false);
  }
  Transformer operator<<(ComplexExpression const& pattern) && { return std::move(*this) < pattern; }

  Transformer operator<(Symbol const& pattern) && {
    bool const matchesAnySentinel = pattern.getName() == sentinel::Any_.getName();
    bool const matched = isActive && matchesAnySentinel;
    return Transformer(std::move(c), matched ? sentinel::AnyHead : nullptr, isActive, false);
  }
  Transformer operator<<(Symbol const& pattern) && { return std::move(*this) < pattern; }

  /*implicit*/ operator Expression() { // NOLINT(hicpp-explicit-conversions)
    return std::visit([](auto&& x) -> Expression { return std::forward<decltype(x)>(x); },
                      std::move(c));
  }
};

Transformer operator<(Expression&& e, char const* stringPattern) {
  return Transformer(std::move(e), sentinel::AnyHead) < stringPattern;
}
Transformer operator<<(Expression&& e, char const* stringPattern) {
  return std::move(e) < stringPattern;
}

// Handles ComplexExpression and ComplexExpressionWithStaticArguments<Ts...>
template <typename E1, typename E2,
          std::enable_if_t<std::is_constructible_v<ComplexExpression, E1&&> &&
                               std::is_constructible_v<ComplexExpression, E2&&>,
                           int> = 0>
Transformer operator<(E1&& subject, E2&& pattern) {
  return Transformer(ComplexExpression(std::forward<E1>(subject)), sentinel::AnyHead) <
         ComplexExpression(std::forward<E2>(pattern));
}
template <typename E1, typename E2,
          std::enable_if_t<std::is_constructible_v<ComplexExpression, E1&&> &&
                               std::is_constructible_v<ComplexExpression, E2&&>,
                           int> = 0>
Transformer operator<<(E1&& subject, E2&& pattern) {
  return std::forward<E1>(subject) < std::forward<E2>(pattern);
}
// Handles Expression (the variant) as subject — excluded for Transformer (to avoid ambiguity with
// its member operator<, since Transformer implicitly converts to Expression) and for types directly
// constructible to ComplexExpression (already handled by the E1,E2 template above)
template <typename Subject, typename E,
          std::enable_if_t<!std::is_same_v<std::decay_t<Subject>, Transformer> &&
                               !std::is_constructible_v<ComplexExpression, Subject&&> &&
                               std::is_convertible_v<Subject&&, Expression> &&
                               std::is_constructible_v<ComplexExpression, E&&>,
                           int> = 0>
Transformer operator<(Subject&& subject, E&& pattern) {
  ComplexExpression complPattern(std::forward<E>(pattern));
  return Transformer(Expression(std::forward<Subject>(subject)), sentinel::AnyHead)
      .operator<(complPattern);
}
template <typename Subject, typename E,
          std::enable_if_t<!std::is_same_v<std::decay_t<Subject>, Transformer> &&
                               !std::is_constructible_v<ComplexExpression, Subject&&> &&
                               std::is_convertible_v<Subject&&, Expression> &&
                               std::is_constructible_v<ComplexExpression, E&&>,
                           int> = 0>
Transformer operator<<(Subject&& subject, E&& pattern) {
  return std::forward<Subject>(subject) < std::forward<E>(pattern);
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
