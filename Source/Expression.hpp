#pragma once
#include "Utilities.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace boss {
class Symbol {
  std::string name;

public:
  explicit Symbol(std::string const& name) : name(name){};
  explicit Symbol(std::string&& name) : name(std::move(name)){};
  std::string const& getName() const { return name; };
  inline bool operator==(boss::Symbol const& s2) const { return getName() == s2.getName(); };
  inline bool operator!=(boss::Symbol const& s2) const { return getName() != s2.getName(); };
};

template <typename T, typename... Args> struct variant_amend;

template <typename... Args0, typename... Args1>
struct variant_amend<std::variant<Args0...>, Args1...> {
  using type = std::variant<Args0..., Args1...>;
};

template <typename Scalar> struct Span {
private: // state
  std::vector<Scalar> adaptee = {};
  Scalar* _begin = nullptr;
  Scalar* _end = nullptr;

public: // surface
  size_t size() const { return _end - _begin; }
  constexpr Scalar const& operator[](size_t i) const { return _begin[i]; }
  constexpr Scalar& operator[](size_t i) { return _begin[i]; }
  auto begin() const { return _begin; }
  auto end() const { return _end; }

  constexpr Scalar const& at(size_t i) const {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    if(_begin + i < _end) {
      return (*this)[i];
    }
    throw std::out_of_range("Span has no element with index " + std::to_string(i));
  }
  constexpr Scalar& at(size_t i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    if(_begin + i < _end) {
      return (*this)[i];
    }
    throw std::out_of_range("Span has no element with index " + std::to_string(i));
  }
  explicit Span(std::vector<Scalar>&& adaptee)
      : adaptee(adaptee), _begin(this->adaptee.data()),
        _end(this->adaptee.data() + this->adaptee.size()) {}

  bool operator==(Span const& other) const { return _begin == other._begin; }

  Span() noexcept = default;
  Span(Span&&) noexcept = default;
  Span(Span const&) = delete;
  Span& operator=(Span&&) noexcept = default;
  Span& operator=(Span const&) = delete;
  ~Span() = default;

  friend std::ostream& operator<<(std::ostream& s, Span const& span) { return s << span.size; }
};

template <typename... AdditionalCustomAtoms>
using AtomicExpressionWithAdditionalCustomAtoms =
    std::variant<bool, std::int64_t, std::double_t, std::string, Symbol, AdditionalCustomAtoms...>;

template <typename StaticArgumentsTuple, typename... AdditionalCustomAtoms>
class ComplexExpressionWithAdditionalCustomAtoms;

template <typename... AdditionalCustomAtoms>
class ExpressionWithAdditionalCustomAtoms
    : public variant_amend<AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>,
                           ComplexExpressionWithAdditionalCustomAtoms<
                               std::tuple<>, AdditionalCustomAtoms...>>::type {
public:
  using SuperType = typename variant_amend<
      AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>,
      ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>, AdditionalCustomAtoms...>>::type;

  using SuperType::SuperType;
  explicit ExpressionWithAdditionalCustomAtoms(int v) noexcept
      : ExpressionWithAdditionalCustomAtoms(long(v)) {}
  explicit ExpressionWithAdditionalCustomAtoms(float v) noexcept
      : ExpressionWithAdditionalCustomAtoms(double(v)) {}
  template <typename = std::enable_if<sizeof...(AdditionalCustomAtoms) != 0>, typename... T>
  ExpressionWithAdditionalCustomAtoms( // NOLINT(hicpp-explicit-conversions)
      ExpressionWithAdditionalCustomAtoms<T...>&& o) noexcept
      : SuperType(std::visit(
            utilities::overload(
                [](ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>, T...>&& unpacked)
                    -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
                  return ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>,
                                                                    AdditionalCustomAtoms...>(
                      std::forward<decltype(unpacked)>(unpacked));
                },
                [](auto&& unpacked) {
                  return ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>(
                      std::forward<decltype(unpacked)>(unpacked));
                }),
            (typename variant_amend<
                 AtomicExpressionWithAdditionalCustomAtoms<T...>,
                 ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>, T...>>::type &&)
                std::move(o))) {}

  ~ExpressionWithAdditionalCustomAtoms() = default;
  ExpressionWithAdditionalCustomAtoms(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;
  ExpressionWithAdditionalCustomAtoms&
  operator=(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;

  template <typename T>
  std::enable_if_t<utilities::isInstanceOfTemplate<
                       std::decay_t<T>, ComplexExpressionWithAdditionalCustomAtoms>::value,
                   bool>
  operator==(T const& other) const {
    return std::holds_alternative<boss::ComplexExpressionWithAdditionalCustomAtoms<
               std::tuple<>, AdditionalCustomAtoms...>>(*this) &&
           (std::get<boss::ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>,
                                                                      AdditionalCustomAtoms...>>(
                *this) == other);
  }

  template <typename T>
  std::enable_if_t<boss::utilities::isVariantMember<T, AtomicExpressionWithAdditionalCustomAtoms<
                                                           AdditionalCustomAtoms...>>::value,
                   bool>
  operator==(T const& other) const {
    if(!std::holds_alternative<T>(*this)) {
      return false;
    }
    return std::get<T>(*this) == other;
  }
  template <typename T>
  std::enable_if_t<!std::is_same_v<T, ExpressionWithAdditionalCustomAtoms>, bool>
  operator!=(T const& other) const {
    return !(*this == other);
  }

  ExpressionWithAdditionalCustomAtoms clone() const {
    using ComplexExpression =
        ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>, AdditionalCustomAtoms...>;
    return std::visit(
        boss::utilities::overload(
            [](auto const& val) -> ExpressionWithAdditionalCustomAtoms { return val; },
            [](ComplexExpression const& val) -> ExpressionWithAdditionalCustomAtoms {
              return ComplexExpression(val.clone());
            }),
        (ExpressionWithAdditionalCustomAtoms::SuperType const&)*this);
  }

private:
  ExpressionWithAdditionalCustomAtoms(ExpressionWithAdditionalCustomAtoms const&) = // NOLINT
      default;
  ExpressionWithAdditionalCustomAtoms&
  operator=(ExpressionWithAdditionalCustomAtoms const&) = default; // NOLINT
};

template <typename... AdditionalCustomAtoms>
using ExpressionArgumentsWithAdditionalCustomAtoms =
    std::vector<ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>;

template <typename... AdditionalCustomAtoms>
using ExpressionSpanArgumentsWithAdditionalCustomAtoms =
    std::vector<std::variant<Span<bool>, Span<std::int64_t>, Span<std::double_t>, Span<std::string>,
                             Span<Symbol>, Span<AdditionalCustomAtoms>...>>;

template <bool ConstWrappee = false, typename... AdditionalCustomAtoms> class ArgumentWrapper;
template <typename... AdditionalCustomAtoms>
using ArgumentWrappeeType = typename variant_amend<
    typename utilities::rewrap_variant_arguments<
        std::reference_wrapper,
        AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>::type,
    std::reference_wrapper<ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>>::type;

template <typename... AdditionalCustomAtoms>
using ConstArgumentWrappeeType = typename variant_amend<
    typename utilities::rewrap_variant_arguments_and_add_const<
        std::reference_wrapper,
        AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>::type,
    std::reference_wrapper<ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const>>::
    type;

namespace std {
using ::std::get;
using ::std::visit;
using namespace ::std;

template <size_t T, bool ConstWrappee = false, typename... AdditionalCustomAtoms>
std::variant_alternative_t<T, ArgumentWrappeeType<AdditionalCustomAtoms...>>&
get(boss::ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...>& wrapper);
} // namespace std

template <bool ConstWrappee, typename... AdditionalCustomAtoms> class ArgumentWrapper {
public:
  using WrappeeType =
      std::conditional_t<ConstWrappee, ConstArgumentWrappeeType<AdditionalCustomAtoms...>,
                         ArgumentWrappeeType<AdditionalCustomAtoms...>>;

private:
  WrappeeType argument;

public:
  WrappeeType& getArgument() & { return argument; };
  WrappeeType&& getArgument() && { return argument; };
  WrappeeType const& getArgument() const& { return argument; };

  operator // NOLINT(hicpp-explicit-conversions)
      ArgumentWrapper<true, AdditionalCustomAtoms...>() const {
    return std::visit(
        [](auto&& argument) {
          return ArgumentWrapper<true, AdditionalCustomAtoms...>(argument.get());
        },
        argument);
  };

  template <typename T> ArgumentWrapper& operator=(T&& newValue) {
    std::get<std::reference_wrapper<T>>(argument).get() = std::forward<T>(newValue);
    return *this;
  }
  template <typename T> ArgumentWrapper& operator=(T& newValue) {
    argument = newValue;
    return *this;
  }

  /**
   * Only allow (move-)conversion to Expressions if the wrapper is non-const
   */
  template <bool Enable = !ConstWrappee,
            typename = typename std::enable_if<Enable>::type>
  operator // NOLINT(hicpp-explicit-conversions)
      ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>() && {
    return std::move(std::visit(
        [](auto&& e) -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
          if constexpr(utilities::isInstanceOfTemplate<std::decay_t<decltype(e)>,
                                                       std::reference_wrapper>::value) {
            return std::move(e.get());
          } else {
            return e;
          }
        },
        std::move(argument)));
  }

  template <typename T,
            typename = std::enable_if_t<std::disjunction<
                utilities::isVariantMember<std::reference_wrapper<T>, WrappeeType>,
                utilities::isVariantMember<std::reference_wrapper<const T>, WrappeeType>>::value>>
  ArgumentWrapper(T& argument) // NOLINT(hicpp-explicit-conversions)
      : argument(std::reference_wrapper(argument)) {}
  bool valueless_by_exception() const { return argument.valueless_by_exception(); }

  auto at(size_t i) {
    return std::visit(boss::utilities::overload([i](auto&& arg) { return arg.at(i); }));
  }

  auto clone() const {
    return std::visit(
        [](auto const& a) {
          if constexpr(utilities::isInstanceOfTemplate<
                           typename std::decay_t<typename std::decay_t<decltype(a)>::type>,
                           ExpressionWithAdditionalCustomAtoms>::value) {
            return a.get().clone();
          } else {
            return ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>(a);
          }
        },
        argument);
  }

  template <typename T> auto operator==(T const& other) const {
    auto constexpr otherIsConstMember =
        utilities::isVariantMember<std::reference_wrapper<const std::decay_t<T>>,
                                   WrappeeType>::value;
    auto constexpr otherIsMember =
        utilities::isVariantMember<std::reference_wrapper<std::decay_t<T>>, WrappeeType>::value;
    auto constexpr otherIsArgumentWrapper =
        std::is_same_v<T, boss::ArgumentWrapper<false, AdditionalCustomAtoms...>> ||
        std::is_same_v<T, boss::ArgumentWrapper<true, AdditionalCustomAtoms...>>;
    if constexpr(otherIsConstMember) {
      return std::holds_alternative<std::reference_wrapper<const std::decay_t<T>>>(argument) &&
             std::get<std::reference_wrapper<const std::decay_t<T>>>(argument).get() == other;
    } else if constexpr(otherIsMember) {
      return std::holds_alternative<std::reference_wrapper<std::decay_t<T>>>(argument) &&
             std::get<std::reference_wrapper<std::decay_t<T>>>(argument).get() == other;
    } else if constexpr(otherIsArgumentWrapper) {
      return std::visit([this](auto&& argument) { return *this == argument.get(); },
                        other.getArgument());
    } else {
      return false;
    }
  };

  template <typename T> auto operator!=(T const& other) const { return !(*this == other); }
};

/**
 * utility template for use in constexpr contexts
 */
template <typename...> struct isConstArgumentWrapperType : public std::false_type {};
template <typename... T>
struct isConstArgumentWrapperType<ArgumentWrapper<true, T...>> : public std::true_type {};
template <typename... T>
inline constexpr bool isConstArgumentWrapper = isConstArgumentWrapperType<T...>::value;

template <typename StaticArgumentsContainer, bool IsConstWrapper = false,
          typename... AdditionalAtoms>
class ExpressionArgumentsWithAdditionalCustomAtomsWrapper {
  StaticArgumentsContainer& staticArguments;
  using DynamicArgumentsContainer =
      std::conditional_t<IsConstWrapper,
                         ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalAtoms...> const,
                         ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalAtoms...>>;
  DynamicArgumentsContainer& arguments;
  using SpanArgumentsContainer =
      std::conditional_t<IsConstWrapper,
                         ExpressionSpanArgumentsWithAdditionalCustomAtoms<AdditionalAtoms...> const,
                         ExpressionSpanArgumentsWithAdditionalCustomAtoms<AdditionalAtoms...>>;
  SpanArgumentsContainer& spanArguments;

public:
  ExpressionArgumentsWithAdditionalCustomAtomsWrapper(StaticArgumentsContainer& staticArguments,
                                                      DynamicArgumentsContainer& arguments,
                                                      SpanArgumentsContainer& spanArguments)
      : staticArguments(staticArguments), arguments(arguments), spanArguments(spanArguments) {}

  size_t size() const { return std::tuple_size_v<StaticArgumentsContainer> + arguments.size(); }
  bool empty() const { return size() == 0; }

  template <bool IsConstIterator> struct Iterator {
    using iterator_category = std::random_access_iterator_tag;
    using difference_type = long;
    using reference = boss::ArgumentWrapper<IsConstIterator, AdditionalAtoms...>;
    using value_type =
        typename boss::ArgumentWrapper<IsConstIterator, AdditionalAtoms...>::WrappeeType;
    using pointer =
        typename boss::ArgumentWrapper<IsConstIterator, AdditionalAtoms...>::WrappeeType;

    std::conditional_t<IsConstIterator, ExpressionArgumentsWithAdditionalCustomAtomsWrapper const,
                       ExpressionArgumentsWithAdditionalCustomAtomsWrapper const>
        container;
    size_t i;
    Iterator next() {
      auto n = *this;
      n++;
      return n;
    }
    Iterator operator++() {
      i++;
      return *this;
    }
    Iterator operator--() {
      i--;
      return *this;
    }
    Iterator& operator+=(difference_type n) {
      i += n;
      return *this;
    }
    Iterator operator++(int) {
      auto before = *this;
      i++;
      return before;
    }
    Iterator operator--(int) {
      auto before = *this;
      i--;
      return before;
    }
    std::ptrdiff_t operator-(Iterator const& other) const { return i - other.i; }

    boss::ArgumentWrapper<IsConstIterator, AdditionalAtoms...> operator*() const {
      return container.at(i);
    }
    bool operator==(Iterator const& other) { return i == other.i; }
    bool operator!=(Iterator const& other) { return i != other.i; }
    bool operator<(Iterator const& other) { return i < other.i; }
    bool operator>(Iterator const& other) { return i > other.i; }
  };

  Iterator<IsConstWrapper> begin() const { return {*this, 0}; }

  Iterator<IsConstWrapper> end() const { return {*this, size()}; }

  template <size_t... I>
  constexpr ArgumentWrapper<IsConstWrapper, AdditionalAtoms...>
  getStaticArgument(size_t i, std::index_sequence<I...> /*unused*/) const {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    return std::move(std::array<ArgumentWrapper<IsConstWrapper, AdditionalAtoms...>, sizeof...(I)>{
        std::get<I>(staticArguments)...}[i]);
  }

  template <size_t... I>
  constexpr ArgumentWrapper<IsConstWrapper, AdditionalAtoms...> getStaticArgument(size_t i) const {
    return getStaticArgument(
        i, std::make_index_sequence<std::tuple_size_v<StaticArgumentsContainer>>());
  }

  ArgumentWrapper<IsConstWrapper, AdditionalAtoms...> front() const { return at(0); }

  ArgumentWrapper<IsConstWrapper, AdditionalAtoms...> operator[](size_t i) const {
    if constexpr(std::tuple_size_v < StaticArgumentsContainer >> 0) {
      if(i < std::tuple_size_v<StaticArgumentsContainer>) {
        return getStaticArgument(i);
      }
    } else if((i - std::tuple_size_v<StaticArgumentsContainer>) < arguments.size()) {
      return arguments[i - std::tuple_size_v<StaticArgumentsContainer>];
    } else {
      auto argumentPrefixScan = std::tuple_size_v<StaticArgumentsContainer> + arguments.size();
      for(auto& spanArgument : spanArguments) {
        if(i >= argumentPrefixScan &&
           i < argumentPrefixScan +
                   std::visit([](auto&& spanArgument) { return spanArgument.size(); },
                              spanArgument)) {
          return std::visit(
              [&](auto&& spanArgument) -> ArgumentWrapper<IsConstWrapper, AdditionalAtoms...> {
                return spanArgument[i - argumentPrefixScan];
              },
              spanArgument);
        }
        argumentPrefixScan +=
            std::visit([](auto&& spanArgument) { return spanArgument.size(); }, spanArgument);
      }
    }
    __builtin_unreachable();
  }

  ArgumentWrapper<IsConstWrapper, AdditionalAtoms...> at(size_t i) const {
    if constexpr((std::tuple_size_v<StaticArgumentsContainer>) > 0) {
      if(i < std::tuple_size_v<StaticArgumentsContainer>) {
        return getStaticArgument(i);
      }
    }
    if((i - std::tuple_size_v<StaticArgumentsContainer>) < arguments.size()) {
      return arguments.at(i - std::tuple_size_v<StaticArgumentsContainer>);
    }
    auto argumentPrefixScan = std::tuple_size_v<StaticArgumentsContainer> + arguments.size();
    for(auto& spanArgument : spanArguments) {
      if(i >= argumentPrefixScan &&
         i < argumentPrefixScan + std::visit([](auto& t) { return t.size(); }, spanArgument)) {
        return std::visit(
            [&](auto&& spanArgument) -> ArgumentWrapper<IsConstWrapper, AdditionalAtoms...> {
              return spanArgument.at(i - argumentPrefixScan);
            },
            spanArgument);
      }
      argumentPrefixScan +=
          std::visit([](auto&& spanArgument) { return spanArgument.size(); }, spanArgument);
    }
    throw std::out_of_range("Expression has no argument with index " + std::to_string(i));
  }

  operator // NOLINT(hicpp-explicit-conversions)
      ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalAtoms...>() const {
    ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalAtoms...> result;
    std::copy(std::begin(*this), std::end(*this), back_inserter(result));
    return std::move(result);
  }
};

template <typename StaticArgumentsTuple, typename... AdditionalCustomAtoms>
class ComplexExpressionWithAdditionalCustomAtoms {
private:
  Symbol head;
  StaticArgumentsTuple staticArguments{};
  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> arguments{};
  ExpressionSpanArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> spanArguments{};

public:
  template <size_t... I>
  static StaticArgumentsTuple
  convertToTuple(ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& arguments,
                 std::index_sequence<I...> /*unused*/) {
    return {(std::get<
             std::remove_reference_t<typename std::tuple_element<I, StaticArgumentsTuple>::type>>(
        arguments.at(I)))...};
  }

  template <typename T>
  void cloneIfNecessary(
      ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& result,
      ComplexExpressionWithAdditionalCustomAtoms<T, AdditionalCustomAtoms...> const& e) const {
    result.emplace_back(move(e.clone()));
  }

  template <typename T,
            typename = std::enable_if_t<utilities::isVariantMember<
                T, AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>::value>>
  void
  cloneIfNecessary(ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& result,
                   T e) const {
    result.emplace_back(e);
  }

  template <size_t... I>
  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>
  convertStaticToDynamicArguments(std::index_sequence<I...> /*unused*/) const {
    ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> result;
    result.reserve(sizeof...(I));
    (cloneIfNecessary(result, std::get<I>(staticArguments)), ...);
    return result;
  }

  explicit ComplexExpressionWithAdditionalCustomAtoms(
      Symbol const& head,
      ExpressionSpanArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&& spanArguments)
      : ComplexExpressionWithAdditionalCustomAtoms(head, {}, {}, std::move(spanArguments)) {}

  explicit ComplexExpressionWithAdditionalCustomAtoms(
      Symbol const& head, StaticArgumentsTuple&& staticArguments,
      ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&& arguments = {},
      ExpressionSpanArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&& spanArguments =
          {})
      : head(head), staticArguments(std::move(staticArguments)), arguments(std::move(arguments)),
        spanArguments(std::move(spanArguments)) {}

  template <typename = std::enable_if<std::tuple_size<StaticArgumentsTuple>::value == 0>>
  explicit ComplexExpressionWithAdditionalCustomAtoms(
      Symbol const& head,
      ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&& arguments)
      : ComplexExpressionWithAdditionalCustomAtoms(
            head,
            convertToTuple(
                arguments,
                std::make_index_sequence<std::tuple_size<StaticArgumentsTuple>::value>()),
            {std::move_iterator(
                 next(begin(arguments), std::tuple_size<StaticArgumentsTuple>::value)),
             std::move_iterator(end(arguments))}){};

  operator ComplexExpressionWithAdditionalCustomAtoms< // NOLINT(hicpp-explicit-conversions)
      std::tuple<>, AdditionalCustomAtoms...>() const {
    return move(ComplexExpressionWithAdditionalCustomAtoms< // NOLINT(hicpp-explicit-conversions)
                std::tuple<>, AdditionalCustomAtoms...>(
        head, convertStaticToDynamicArguments(
                  std::make_index_sequence<std::tuple_size<StaticArgumentsTuple>::value>())));
  }

  template <typename = std::enable_if<sizeof...(AdditionalCustomAtoms) != 0>, typename... T>
  explicit ComplexExpressionWithAdditionalCustomAtoms(
      ComplexExpressionWithAdditionalCustomAtoms<T...>&& other)
      : head(other.getHead()) {
    arguments.reserve(other.getArguments().size());
    for(auto&& arg : other.getArguments()) {
      std::visit([this](auto&& arg) { arguments.emplace_back(std::move(arg.get())); },
                 std::move(arg.getArgument()));
    }
  }

  ExpressionArgumentsWithAdditionalCustomAtomsWrapper<decltype(staticArguments), false,
                                                      AdditionalCustomAtoms...>
  getArguments() {
    return {staticArguments, arguments, spanArguments};
  }

  ExpressionArgumentsWithAdditionalCustomAtomsWrapper<decltype(staticArguments) const, true,
                                                      AdditionalCustomAtoms...>
  getArguments() const {
    return {staticArguments, arguments, spanArguments};
  }

  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& getDynamicArguments() {
    return arguments;
  };

  auto const& getStaticArguments() const { return staticArguments; }
  auto const& getSpanArguments() const { return spanArguments; }

  Symbol const& getHead() const { return head; };
  ~ComplexExpressionWithAdditionalCustomAtoms() = default;
  ComplexExpressionWithAdditionalCustomAtoms(
      ComplexExpressionWithAdditionalCustomAtoms&&) noexcept = default;
  ComplexExpressionWithAdditionalCustomAtoms&
  operator=(ComplexExpressionWithAdditionalCustomAtoms&&) noexcept = default;

  bool operator==(ComplexExpressionWithAdditionalCustomAtoms const& other) const {
    if(getHead() != other.getHead() || getArguments().size() != other.getArguments().size()) {
      return false;
    }
    for(auto i = 0U; i < getArguments().size(); i++) {
      if(getArguments()[i] != other.getArguments()[i]) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(ComplexExpressionWithAdditionalCustomAtoms const& other) const {
    return !(*this == other);
  }

  ComplexExpressionWithAdditionalCustomAtoms clone() const {
    ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> copiedArgs;
    copiedArgs.reserve(arguments.size());
    for(auto const& arg : arguments) {
      copiedArgs.emplace_back(arg.clone());
    }

    return ComplexExpressionWithAdditionalCustomAtoms(head, std::move(copiedArgs));
  }

private:
  ComplexExpressionWithAdditionalCustomAtoms(ComplexExpressionWithAdditionalCustomAtoms const&) =
      default;
  ComplexExpressionWithAdditionalCustomAtoms&
  operator=(ComplexExpressionWithAdditionalCustomAtoms const&) = default;
};

template <typename... AdditionalCustomAtoms> class ExtensibleExpressionSystem {
public:
  using AtomicExpression = AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
  template <typename... StaticArgumentTypes>
  using ComplexExpressionWithStaticArguments =
      ComplexExpressionWithAdditionalCustomAtoms<std::tuple<StaticArgumentTypes...>,
                                                 AdditionalCustomAtoms...>;
  using ComplexExpression = ComplexExpressionWithStaticArguments<>;
  using Expression = ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
  using ExpressionArguments =
      ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
  using ExpressionSpanArguments =
      ExpressionSpanArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
};

using DefaultExpressionSystem = ExtensibleExpressionSystem<>;

using AtomicExpression = DefaultExpressionSystem::AtomicExpression;
template <typename... StaticArgumentTypes>
using ComplexExpressionWithStaticArguments =
    DefaultExpressionSystem::ComplexExpressionWithStaticArguments<StaticArgumentTypes...>;
using ComplexExpression = DefaultExpressionSystem::ComplexExpressionWithStaticArguments<>;
using Expression = DefaultExpressionSystem::Expression;
using ExpressionArguments = DefaultExpressionSystem::ExpressionArguments;
using ExpressionSpanArguments = DefaultExpressionSystem::ExpressionSpanArguments;

namespace std {

template <typename T, typename... AdditionalCustomAtoms>
T const& get(ArgumentWrapper<true, AdditionalCustomAtoms...> const& wrapper) {
  return std::visit(
      [](auto const& wrappee) -> T const& {
        if constexpr(utilities::isInstanceOfTemplate<std::decay_t<decltype(wrappee)>,
                                                     std::reference_wrapper>::value) {
          if constexpr(is_same_v<typename std::decay_t<decltype(wrappee)>::type, T>) {
            return wrappee.get();
          } else if constexpr(utilities::isInstanceOfTemplate<
                                  std::decay_t<decltype(wrappee.get())>,
                                  boss::ExpressionWithAdditionalCustomAtoms>::value) {
            return std::get<T>(wrappee.get());
          }
          throw std::bad_variant_access();
        } else {
          return get<T>(wrappee);
        }
      },
      wrapper.getArgument());
}

template <size_t I, auto ConstWrappee = false, typename... AdditionalCustomAtoms>
std::variant_alternative_t<I, ArgumentWrappeeType<AdditionalCustomAtoms...>>&
get(boss::ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...> const& wrapper) {
  return std::get<I>(wrapper.getArgument());
};
template <typename T, auto ConstWrappee = false, typename... AdditionalCustomAtoms>
T& get(boss::ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...> const& wrapper) {
  return std::visit(
      [](auto& argument) -> T& {
        if constexpr(std::is_same_v<std::decay_t<decltype(argument)>, std::reference_wrapper<T>>) {
          return argument.get();
        } else if constexpr(utilities::isInstanceOfTemplate<std::decay_t<decltype(argument)>,
                                                            std::reference_wrapper>::value &&
                            utilities::isInstanceOfTemplate<
                                std::decay_t<decltype(argument.get())>,
                                boss::ExpressionWithAdditionalCustomAtoms>::value) {
          return std::get<T>(argument.get());
        } else {
          throw std::bad_variant_access();
        }
      },
      wrapper.getArgument());
}

template <typename Func, auto ConstWrappee = false, typename... AdditionalCustomAtoms>
decltype(auto) visit(Func&& func,
                     boss::ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...>&& wrapper) {
  return visit(
      [&](auto&& unwrapped) {
        if constexpr(is_same_v<
                         std::remove_reference_t<decltype(unwrapped)>,
                         boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>) {
          return std::visit(std::forward<Func>(func), unwrapped);
        } else {
          return std::forward<Func>(func)(unwrapped);
        }
      },
      wrapper.getArgument());
}

template <typename Func, auto ConstWrappee = false, typename... AdditionalCustomAtoms>
decltype(auto) visit(Func&& func,
                     boss::ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...>& wrapper) {
  return visit(
      [&](auto& unwrapped) {
        if constexpr(is_same_v<
                         std::remove_reference_t<decltype(unwrapped)>,
                         boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>) {
          return std::visit(std::forward<Func>(func), unwrapped);
        } else {
          return std::forward<Func>(func)(unwrapped);
        }
      },
      wrapper.getArgument());
}

template <typename... AdditionalCustomAtoms> struct variant_size;

template <typename... AdditionalCustomAtoms>
struct variant_size<typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_size<
          typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>::SuperType> {
};

template <typename... AdditionalCustomAtoms>
struct variant_size<
    const typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_size<const typename boss::ExpressionWithAdditionalCustomAtoms<
          AdditionalCustomAtoms...>::SuperType> {};

template <std::size_t I, typename... AdditionalCustomAtoms> struct variant_alternative;
template <std::size_t I, typename... AdditionalCustomAtoms>
struct variant_alternative<
    I, typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_alternative<I, typename boss::ExpressionWithAdditionalCustomAtoms<
                                 AdditionalCustomAtoms...>::SuperType> {};
template <typename Func, typename... AdditionalCustomAtoms>
decltype(auto)
visit(Func&& func,
      typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& e) {
  return visit(
      std::forward<Func>(func),
      (typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>::SuperType&)e);
};
template <typename Func, typename... AdditionalCustomAtoms>
decltype(auto)
visit(Func&& func,
      typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const& e) {
  return visit(std::forward<Func>(func), (typename boss::ExpressionWithAdditionalCustomAtoms<
                                             AdditionalCustomAtoms...>::SuperType const&)e);
};
template <typename Func, typename... AdditionalCustomAtoms>
decltype(auto)
visit(Func&& func,
      typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&& e) {
  return visit(
      std::forward<Func>(func),
      (typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>::SuperType &&)
          std::move(e));
};
} // namespace std

} // namespace boss
template <> struct std::hash<boss::Symbol> {
  std::size_t operator()(boss::Symbol const& s) const noexcept {
    return std::hash<std::string>{}(s.getName());
  }
};
namespace std {
template <typename... AdditionalCustomAtoms>
struct variant_size<typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_size<
          typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>::SuperType> {
};
template <typename... AdditionalCustomAtoms>
struct variant_size<
    const typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_size<const typename boss::ExpressionWithAdditionalCustomAtoms<
          AdditionalCustomAtoms...>::SuperType> {};

template <std::size_t I, typename... AdditionalCustomAtoms>
struct variant_alternative<
    I, typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_alternative<I, typename boss::ExpressionWithAdditionalCustomAtoms<
                                 AdditionalCustomAtoms...>::SuperType> {};

} // namespace std
