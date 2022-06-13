#pragma once
#include "Utilities.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

namespace boss {
namespace expressions {
namespace atoms {
class Symbol {
  std::string name;

public:
  explicit Symbol(std::string const& name) : name(name){};
  explicit Symbol(std::string&& name) : name(std::move(name)){};
  std::string const& getName() const { return name; };
  inline bool operator==(Symbol const& s2) const { return getName() == s2.getName(); };
  inline bool operator!=(Symbol const& s2) const { return getName() != s2.getName(); };
  friend ::std::ostream& operator<<(::std::ostream& out, Symbol const& thing) {
    return out << thing.getName();
  }
};

template <typename Scalar> struct Span {
private: // state
  void* adapteePayload = {};
  std::function<void(void*)> destructor;

  std::conditional_t<std::is_same_v<std::remove_const_t<Scalar>, bool>, std::vector<bool>::iterator,
                     Scalar*>
      _begin = {};
  std::conditional_t<std::is_same_v<std::remove_const_t<Scalar>, bool>, std::vector<bool>::iterator,
                     Scalar*>
      _end = {};

public: // surface
  size_t size() const { return _end - _begin; }
  constexpr auto operator[](size_t i) const -> decltype(auto) { return *(_begin + i); }
  constexpr auto operator[](size_t i) -> decltype(auto) { return *(_begin + i); }
  auto begin() const { return _begin; }
  auto end() const { return _end; }

  constexpr auto at(size_t i) const -> decltype(auto) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    if(_begin + i < _end) {
      return (*this)[i];
    }
    throw std::out_of_range("Span has no element with index " + std::to_string(i));
  }
  constexpr auto at(size_t i) -> decltype(auto) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    if(_begin + i < _end) {
      return (*this)[i];
    }
    throw std::out_of_range("Span has no element with index " + std::to_string(i));
  }
  /**
   * for some reason, the span takes ownership of the adaptee. That seems weird to me.
   */
  explicit Span(std::vector<std::remove_const_t<Scalar>>&& adaptee)
      : adapteePayload(new std::vector<std::remove_const_t<Scalar>>(move(adaptee))),
        _begin([this]() {
          if constexpr(std::is_same_v<Scalar, bool>) {
            return static_cast<std::vector<std::remove_const_t<Scalar>>*>(this->adapteePayload)
                ->begin();
          } else {
            return static_cast<std::vector<std::remove_const_t<Scalar>>*>(this->adapteePayload)
                ->data();
          }
        }()),
        _end(_begin +
             static_cast<std::vector<std::remove_const_t<Scalar>>*>(this->adapteePayload)->size()),
        destructor(
            [](void* v) { delete static_cast<std::vector<std::remove_const_t<Scalar>>*>(v); }) {}

  explicit Span(Scalar* begin, size_t size, std::function<void(void*)> destructor)
      : _begin(begin), _end(begin + size), destructor(std::move(destructor)) {}

  bool operator==(Span const& other) const { return _begin == other._begin; }

  Span() noexcept = default;
  Span(Span&& other) noexcept
      : adapteePayload(other.adapteePayload), _begin(other._begin), _end(other._end),
        destructor(move(other.destructor)) {
    other.adapteePayload = nullptr;
    other.destructor = [](void* /* unused */) {};
  };

  /**
   * because the Span constructor cannot infer what data structure/payload was used to hold the
   * values in the other Span, arguments are copied into a std::vector. The alternative would be to
   * use some kind of reference chain but I (Holger) did not like that -- I am open to discussing
   * this, though
   */
  Span(Span<Scalar> const& other)
      : adapteePayload(new std::vector<std::remove_const_t<Scalar>>(other._begin, other._end)),
        _begin([this]() {
          if constexpr(std::is_same_v<std::remove_const_t<Scalar>, bool>) {
            return static_cast<std::vector<std::remove_const_t<Scalar>>*>(this->adapteePayload)
                ->begin();
          } else {
            return static_cast<std::vector<std::remove_const_t<Scalar>>*>(this->adapteePayload)
                ->data();
          }
        }()),
        _end(_begin +
             static_cast<std::vector<std::remove_const_t<Scalar>>*>(this->adapteePayload)->size()),
        destructor(
            [](void* v) { delete static_cast<std::vector<std::remove_const_t<Scalar>>*>(v); }){};

  Span& operator=(Span&&) noexcept = default;
  Span& operator=(Span const&) = delete;
  ~Span() { destructor(adapteePayload); };

  friend std::ostream& operator<<(std::ostream& s, Span const& span) { return s << span.size; }
};
} // namespace atoms
using atoms::Span;
using atoms::Symbol;

template <typename... AdditionalCustomAtoms>
using AtomicExpressionWithAdditionalCustomAtoms =
    std::variant<bool, std::int64_t, std::double_t, std::string, Symbol, AdditionalCustomAtoms...>;

namespace generic {

template <typename StaticArgumentsTuple, typename... AdditionalCustomAtoms>
class ComplexExpressionWithAdditionalCustomAtoms;
template <typename T>
inline constexpr bool isComplexExpression =
    boss::utilities::isInstanceOfTemplate<std::decay_t<T>,
                                          ComplexExpressionWithAdditionalCustomAtoms>::value;

template <typename... AdditionalCustomAtoms>
class ExpressionWithAdditionalCustomAtoms
    : public boss::utilities::variant_amend<
          AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>,
          ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>,
                                                     AdditionalCustomAtoms...>>::type {
public:
  using SuperType = typename boss::utilities::variant_amend<
      AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>,
      ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>, AdditionalCustomAtoms...>>::type;

  using SuperType::SuperType;
  explicit ExpressionWithAdditionalCustomAtoms(int32_t v) noexcept
      : ExpressionWithAdditionalCustomAtoms(int64_t(v)) {}
  explicit ExpressionWithAdditionalCustomAtoms(float_t v) noexcept
      : ExpressionWithAdditionalCustomAtoms(double_t(v)) {}
  template <typename = std::enable_if<sizeof...(AdditionalCustomAtoms) != 0>, typename... T>
  ExpressionWithAdditionalCustomAtoms( // NOLINT(hicpp-explicit-conversions)
      ExpressionWithAdditionalCustomAtoms<T...>&& o) noexcept
      : SuperType(std::visit(
            boss::utilities::overload(
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
            (typename boss::utilities::variant_amend<
                 AtomicExpressionWithAdditionalCustomAtoms<T...>,
                 ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>, T...>>::type &&)
                std::move(o))) {}

  ~ExpressionWithAdditionalCustomAtoms() = default;
  ExpressionWithAdditionalCustomAtoms(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;
  ExpressionWithAdditionalCustomAtoms&
  operator=(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;

  template <typename T>
  std::enable_if_t<boss::utilities::isInstanceOfTemplate<
                       std::decay_t<T>, ComplexExpressionWithAdditionalCustomAtoms>::value,
                   bool>
  operator==(T const& other) const {
    return std::holds_alternative<
               ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>, AdditionalCustomAtoms...>>(
               *this) &&
           (std::get<
                ComplexExpressionWithAdditionalCustomAtoms<std::tuple<>, AdditionalCustomAtoms...>>(
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

  friend ::std::ostream& operator<<(::std::ostream& out,
                                    ExpressionWithAdditionalCustomAtoms const& thing) {
    visit(
        boss::utilities::overload([&](::std::string const& value) { out << "\"" << value << "\""; },
                                  [&](bool value) { out << (value ? "True" : "False"); },
                                  [&](auto const& value) { out << value; }),
        thing);
    return out;
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
class ExpressionSpanArgumentsWithAdditionalCustomAtoms
    : public std::vector<
          std::variant<Span<bool>, Span<std::int64_t>, Span<std::double_t>, Span<std::string>,
                       Span<Symbol>, Span<AdditionalCustomAtoms>..., Span<bool const>,
                       Span<std::int64_t const>, Span<std::double_t const>, Span<std::string const>,
                       Span<Symbol const>, Span<AdditionalCustomAtoms const>...>> {
public:
  using std::vector<
      std::variant<Span<bool>, Span<std::int64_t>, Span<std::double_t>, Span<std::string>,
                   Span<Symbol>, Span<AdditionalCustomAtoms>..., Span<bool const>,
                   Span<std::int64_t const>, Span<std::double_t const>, Span<std::string const>,
                   Span<Symbol const>, Span<AdditionalCustomAtoms const>...>>::vector;
};

template <class T> class MovableReferenceWrapper {
public:
  typedef T type;

  explicit MovableReferenceWrapper(std::reference_wrapper<T>&& ref) {
    _ptr = std::addressof(ref.get());
  }

  MovableReferenceWrapper(MovableReferenceWrapper const&) noexcept = default;
  MovableReferenceWrapper& operator=(MovableReferenceWrapper const&) noexcept = default;
  MovableReferenceWrapper(MovableReferenceWrapper&&) noexcept = default;
  MovableReferenceWrapper& operator=(MovableReferenceWrapper&&) noexcept = default;
  ~MovableReferenceWrapper() = default;

  constexpr explicit operator T&() const& { return *_ptr; }
  constexpr T& get() const& { return *_ptr; }

  constexpr explicit operator T&&() && { return std::move(*_ptr); }
  constexpr T get() && { return std::move(*_ptr); }

private:
  T* _ptr;
};

template <bool ConstWrappee = false, typename... AdditionalCustomAtoms> class ArgumentWrapper;
template <typename... AdditionalCustomAtoms>
using ArgumentWrappeeType = typename boss::utilities::variant_amend<
    typename boss::utilities::rewrap_variant_arguments<
        MovableReferenceWrapper,
        AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>::type,
    std::vector<bool>::reference,
    MovableReferenceWrapper<ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>>::type;

template <typename... AdditionalCustomAtoms>
using ConstArgumentWrappeeType = typename boss::utilities::variant_amend<
    typename boss::utilities::rewrap_variant_arguments_and_add_const<
        MovableReferenceWrapper,
        AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>::type,
    std::vector<bool>::const_reference,
    MovableReferenceWrapper<ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const>>::
    type;

template <typename T, typename... AdditionalCustomAtoms>
T const& get(ArgumentWrapper<true, AdditionalCustomAtoms...> const& wrapper) {
  return ::std::visit(
      [](auto const& wrappee) -> T const& {
        if constexpr(boss::utilities::isInstanceOfTemplate<::std::decay_t<decltype(wrappee)>,
                                                           MovableReferenceWrapper>::value) {
          if constexpr(::std::is_same_v<typename ::std::decay_t<decltype(wrappee)>::type, T>) {
            return wrappee.get();
          } else if constexpr(boss::utilities::isInstanceOfTemplate<
                                  ::std::decay_t<decltype(wrappee.get())>,
                                  ExpressionWithAdditionalCustomAtoms>::value) {
            return std::get<T>(wrappee.get());
          }
          throw ::std::bad_variant_access();
        } else if constexpr(::std::is_same_v<::std::decay_t<decltype(wrappee)>,
                                             ::std::vector<bool>::reference> ||
                            ::std::is_same_v<::std::decay_t<decltype(wrappee)>,
                                             ::std::vector<bool>::const_reference>) {
          if constexpr(::std::is_same_v<bool, T>) {
            return wrappee;
          }
          throw ::std::bad_variant_access();
        } else {
          return get<T>(wrappee);
        }
      },
      wrapper.getArgument());
}

template <bool ConstWrappee, typename... AdditionalCustomAtoms> class ArgumentWrapper {
public:
  using WrappeeType =
      std::conditional_t<ConstWrappee, ConstArgumentWrappeeType<AdditionalCustomAtoms...>,
                         ArgumentWrappeeType<AdditionalCustomAtoms...>>;

private:
  WrappeeType argument;

public:
  WrappeeType& getArgument() & { return argument; };
  WrappeeType getArgument() && { return std::move(argument); };
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
    std::get<MovableReferenceWrapper<T>>(argument).get() = std::forward<T>(newValue);
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
          if constexpr(boss::utilities::isInstanceOfTemplate<std::decay_t<decltype(e)>,
                                                             MovableReferenceWrapper>::value) {
            return std::move(e.get());
          } else {
            return e;
          }
        },
        std::move(argument)));
  }

  /**
   * we allow conversion to Expression value only for lvalues (to disambiguate
   * from conversion to rvalue)
   */
  operator // NOLINT(hicpp-explicit-conversions)
      ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&() & {
    // this is a shim and should be removed
    return std::visit(
        [](auto& e) -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
          if constexpr(boss::utilities::isInstanceOfTemplate<std::decay_t<decltype(e)>,
                                                             MovableReferenceWrapper>::value) {
            if constexpr(boss::utilities::isInstanceOfTemplate<
                             std::decay_t<decltype(e.get())>,
                             ExpressionWithAdditionalCustomAtoms>::value) {
              return e.get().clone();
            } else {
              return e.get();
            }
          } else {
            return e;
          }
        },
        argument);
  }

  /**
   * we allow conversion to rvalue Expression only for rvalues
   */
  operator // NOLINT(hicpp-explicit-conversions)
      ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>() && {
    // this is a shim and should be removed
    return std::visit(
        [](auto&& e) -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
          if constexpr(boss::utilities::isInstanceOfTemplate<std::decay_t<decltype(e)>,
                                                             MovableReferenceWrapper>::value) {
            return std::move(e.get());
          } else {
            return e;
          }
        },
        std::move(argument));
  }

  /**
   * ArgumentWrappers wrap statically typed references to atomic types or references to dynamically
   * typed boss expressions. The provide a unified (dynamically-typed, visitor-based) interface to
   * them these types.
   */
  template <typename T,
            typename = std::enable_if_t<std::conjunction<
                std::negation<boss::utilities::isVariantMember<MovableReferenceWrapper<const T>,
                                                               WrappeeType>>,
                boss::utilities::isVariantMember<MovableReferenceWrapper<T>, WrappeeType>>::value>>
  ArgumentWrapper(T& argument) // NOLINT(hicpp-explicit-conversions)
      : argument(MovableReferenceWrapper(std::ref(argument))) {}
  template <
      typename T,
      typename = std::enable_if_t<std::conjunction<
          std::negation<boss::utilities::isVariantMember<MovableReferenceWrapper<T>, WrappeeType>>,
          boss::utilities::isVariantMember<MovableReferenceWrapper<const T>, WrappeeType>>::value>>
  ArgumentWrapper(T const& argument) // NOLINT(hicpp-explicit-conversions)
      : argument(MovableReferenceWrapper(std::cref(argument))) {}
  template <typename T, typename = std::enable_if_t<
                            std::disjunction_v<std::is_same<T, std::vector<bool>::const_reference>,
                                               std::is_same<T, std::vector<bool>::reference>>>>
  ArgumentWrapper(T&& argument) // NOLINT(hicpp-explicit-conversions)
      : argument([&argument]() {
          if constexpr(ConstWrappee) {
            return static_cast<std::vector<bool>::const_reference>(argument);
          } else {
            return static_cast<std::vector<bool>::reference>(argument);
          }
        }()) {}

  bool valueless_by_exception() const { return argument.valueless_by_exception(); }

  auto at(size_t i) {
    return std::visit(boss::utilities::overload([i](auto&& arg) { return arg.at(i); }));
  }

  auto clone() const {
    static auto unwrap = [](auto const& b) {
      if constexpr(boss::utilities::isInstanceOfTemplate<
                       std::decay_t<decltype(b)>, ExpressionWithAdditionalCustomAtoms>::value) {
        return b.clone();
      } else {
        return ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>(b);
      }
    };
    return std::visit(
        boss::utilities::overload(
            [](auto const& a) -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
              if constexpr(boss::utilities::isInstanceOfTemplate<
                               std::decay_t<decltype(a)>,
                               ExpressionWithAdditionalCustomAtoms>::value) {
                return a.get().clone();
              }
              if constexpr(boss::utilities::isInstanceOfTemplate<std::decay_t<decltype(a)>,
                                                                 MovableReferenceWrapper>::value) {
                return unwrap(a.get());
              } else {
                return ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>(a);
              }
            }),
        argument);
  }

  template <typename T> auto operator==(T const& other) const {
    auto constexpr otherIsConstMember =
        boss::utilities::isVariantMember<MovableReferenceWrapper<const std::decay_t<T>>,
                                         WrappeeType>::value;
    auto constexpr otherIsMember =
        boss::utilities::isVariantMember<MovableReferenceWrapper<std::decay_t<T>>,
                                         WrappeeType>::value;
    auto constexpr otherIsArgumentWrapper =
        std::is_same_v<T, ArgumentWrapper<false, AdditionalCustomAtoms...>> ||
        std::is_same_v<T, ArgumentWrapper<true, AdditionalCustomAtoms...>>;
    if constexpr(otherIsConstMember) {
      return std::holds_alternative<MovableReferenceWrapper<const std::decay_t<T>>>(argument) &&
             std::get<MovableReferenceWrapper<const std::decay_t<T>>>(argument).get() == other;
    } else if constexpr(otherIsMember) {
      return std::holds_alternative<MovableReferenceWrapper<std::decay_t<T>>>(argument) &&
             std::get<MovableReferenceWrapper<std::decay_t<T>>>(argument).get() == other;
    } else if constexpr(otherIsArgumentWrapper) {
      return std::visit(
          boss::utilities::overload(
              [this](std::vector<bool>::const_reference argument) { return *this == argument; },
              [this](auto&& argument) { return *this == argument.get(); }),
          other.getArgument());
    } else {
      return false;
    }
  };

  template <typename T> auto operator!=(T const& other) const { return !(*this == other); }

  friend ::std::ostream& operator<<(::std::ostream& stream, ArgumentWrapper const& argument) {
    return visit(
        [&stream](auto&& argument) -> auto& {
          if constexpr(::std::disjunction_v<::std::is_same<::std::decay_t<decltype(argument)>,
                                                           ::std::vector<bool>::reference>,
                                            ::std::is_same<::std::decay_t<decltype(argument)>,
                                                           ::std::vector<bool>::const_reference>>) {
            return stream << (bool)argument;
          } else {
            return stream << argument.get();
          }
        },
        argument.getArgument());
  }
};

template <typename Func, auto ConstWrappee, typename... AdditionalCustomAtoms>
decltype(auto) visit(Func&& func,
                     ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...> const& wrapper) {
  return visit(
      [&](auto&& unwrapped) {
        if constexpr(boss::utilities::isInstanceOfTemplate<::std::decay_t<decltype(unwrapped)>,
                                                           MovableReferenceWrapper>::value) {
          if constexpr(::std::is_same_v<
                           ::std::remove_reference_t<decltype(unwrapped.get())>,
                           ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>) {
            return visit(::std::forward<Func>(func), unwrapped.get());
          } else {
            return ::std::forward<Func>(func)(unwrapped.get());
          }
        } else if constexpr(::std::is_same_v<
                                ::std::remove_reference_t<decltype(unwrapped)>,
                                ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>) {
          return visit(::std::forward<Func>(func), unwrapped);
        } else {
          return ::std::forward<Func>(func)(unwrapped);
        }
      },
      wrapper.getArgument());
}

namespace utilities {
/**
 * utility template for use in constexpr contexts
 */
template <typename...> struct isConstArgumentWrapperType : public std::false_type {};
template <typename... T>
struct isConstArgumentWrapperType<ArgumentWrapper<true, T...>> : public std::true_type {};
template <typename... T>
inline constexpr bool isConstArgumentWrapper = isConstArgumentWrapperType<T...>::value;
} // namespace utilities

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

  size_t size() const {
    return std::tuple_size_v<StaticArgumentsContainer> + arguments.size() +
           std::accumulate(
               spanArguments.begin(), spanArguments.end(), 0, [](auto soFar, auto& thisOne) {
                 return soFar + std::visit([](auto&& thisOne) { return thisOne.size(); }, thisOne);
               });
  }
  bool empty() const { return size() == 0; }

  template <bool IsConstIterator> struct Iterator {
    using iterator_category = std::random_access_iterator_tag;
    using difference_type = long;
    using reference = ArgumentWrapper<IsConstIterator, AdditionalAtoms...>;
    using value_type = typename ArgumentWrapper<IsConstIterator, AdditionalAtoms...>::WrappeeType;
    using pointer = typename ArgumentWrapper<IsConstIterator, AdditionalAtoms...>::WrappeeType;

    std::reference_wrapper<ExpressionArgumentsWithAdditionalCustomAtomsWrapper const> container;
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
    Iterator operator+(int i) {
      auto result = *this;
      result.i += i;
      return result;
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

    ArgumentWrapper<IsConstIterator, AdditionalAtoms...> operator*() const {
      return container.get().at(i);
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
                if constexpr((std::is_const_v<std::remove_reference_t<decltype(spanArgument)>> ||
                              std::is_const_v<std::remove_reference_t<decltype(spanArgument.at(
                                  0))>>)&&!IsConstWrapper) {
                  throw std::runtime_error("cannot convert const span to non-const argument");
                } else {
                  return spanArgument[i - argumentPrefixScan];
                }
              },
              spanArgument);
        }
        argumentPrefixScan +=
            std::visit([](auto&& spanArgument) { return spanArgument.size(); }, spanArgument);
      }
    }
#if defined(_MSC_VER)
    __assume(0);
#else
    __builtin_unreachable();
#endif
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
              if constexpr((std::is_const_v<std::remove_reference_t<decltype(spanArgument)>> ||
                            std::is_const_v<std::remove_reference_t<decltype(spanArgument.at(
                                0))>>)&&!IsConstWrapper) {
                throw std::runtime_error("cannot convert const span to non-const argument");
              }

              else if constexpr(std::is_same_v<std::decay_t<decltype(spanArgument.at(0))>,
                                               std::vector<bool>::reference>) {
                if constexpr(IsConstWrapper) {
                  return std::vector<bool>::const_reference(
                      spanArgument.at(i - argumentPrefixScan));
                } else {
                  return std::vector<bool>::reference(spanArgument.at(i - argumentPrefixScan));
                }
              } else {
                return spanArgument.at(i - argumentPrefixScan);
              }
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

  template <typename T> void emplace_back(T t) {
    assert(spanArguments.size() == 0);
    arguments.emplace_back(t);
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
            typename = std::enable_if_t<boss::utilities::isVariantMember<
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

  ComplexExpressionWithAdditionalCustomAtoms(
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
      std::visit(boss::utilities::overload(
                     [this](std::vector<bool>::reference&& arg) {
                       arguments.emplace_back(std::move(arg));
                     },
                     [this](auto&& arg) { arguments.emplace_back(std::move(arg.get())); }),
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

  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const&
  getDynamicArguments() const {
    return arguments;
  };
  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& getDynamicArguments() {
    return arguments;
  };

  auto const& getStaticArguments() const { return staticArguments; }
  auto& getStaticArguments() { return staticArguments; }

  auto const& getSpanArguments() const { return spanArguments; }
  auto& getSpanArguments() { return spanArguments; }

  ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> getArgument(size_t i) && {
    return visit(
        [](auto&& unwrapped) -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
          if constexpr(boss::utilities::isInstanceOfTemplate<::std::decay_t<decltype(unwrapped)>,
                                                             MovableReferenceWrapper>::value) {
            if constexpr(::std::is_same_v<
                             ::std::remove_reference_t<decltype(unwrapped.get())>,
                             ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>) {
              return visit(
                  [](auto&& arg) -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
                    return ::std::forward<decltype(arg)>(arg);
                  },
                  ::std::forward<decltype(unwrapped)>(unwrapped).get());
            } else {
              return ::std::forward<decltype(unwrapped)>(unwrapped).get();
            }
          } else if constexpr(::std::is_same_v<
                                  ::std::remove_reference_t<decltype(unwrapped)>,
                                  ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>) {
            return visit(
                [](auto&& arg) -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
                  return ::std::forward<decltype(arg)>(arg);
                },
                ::std::forward<decltype(unwrapped)>(unwrapped));
          } else {
            return ::std::forward<decltype(unwrapped)>(unwrapped);
          }
        },
        std::forward<decltype(getArguments().at(i).getArgument())>(
            getArguments().at(i).getArgument()));
  }

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
    ExpressionSpanArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> newSpanArguments;
    static_assert(std::tuple_size_v<decltype(staticArguments)> == 0);
    for(auto const& arg : getDynamicArguments()) {
      copiedArgs.emplace_back(arg.clone());
    }

    newSpanArguments.reserve(spanArguments.size());
    for(auto& it : spanArguments) {
      newSpanArguments.push_back(it);
    }

    return ComplexExpressionWithAdditionalCustomAtoms(head, {}, std::move(copiedArgs),
                                                      std::move(newSpanArguments));
  }

  /**
   * a specialization for complex expressions is needed. Otherwise the complex
   * expression and all its arguments have to be copied to be converted to an
   * Expression
   */
  friend ::std::ostream& operator<<(::std::ostream& out,
                                    ComplexExpressionWithAdditionalCustomAtoms const& e) {
    out << e.getHead() << "[";
    if(!e.getArguments().empty()) {
      out << e.getArguments().front();
      for(auto it = ::std::next(e.getArguments().begin()); it != e.getArguments().end(); ++it) {
        out << "," << *it;
      }
    }
    out << "]";
    return out;
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

template <typename TargetType, typename VariantType>
static auto formatBOSSBadVariantAccess(VariantType const& v) {
  ::std::stringstream s;
  s << "expected and actual type mismatch in expression \"";
  if(!v.valueless_by_exception()) {
    s << v;
  } else {
    s << "valueless by exception";
  }
  static auto typenames =
      ::std::map<::std::type_index, char const*>{{typeid(int64_t), "long"},
                                                 {typeid(Symbol), "Symbol"},
                                                 {typeid(bool), "bool"},
                                                 {typeid(double_t), "double"},
                                                 {typeid(::std::string), "string"}};
  s << "\", expected "
    << (typenames.count(typeid(TargetType)) ? typenames.at(typeid(TargetType))
                                            : typeid(TargetType).name());
  return boss::utilities::bad_variant_access(s.str());
}

template <typename T, typename... AdditionalCustomAtoms>
T const& get(generic::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const& e) {
  try {
    return std::get<T>(e);
  } catch(std::bad_variant_access&) {
    throw formatBOSSBadVariantAccess<T>(e);
  }
}

template <typename T, typename... AdditionalCustomAtoms>
T& get(generic::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& e) {
  try {
    return std::get<T>(e);
  } catch(std::bad_variant_access&) {
    throw formatBOSSBadVariantAccess<T>(e);
  }
}

template <typename T, typename... AdditionalCustomAtoms>
T get(generic::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&& e) {
  try {
    return std::get<T>(std::move(e));
  } catch(std::bad_variant_access&) {
    throw formatBOSSBadVariantAccess<T>(e);
  }
}

template <typename T, auto ConstWrappee, typename... AdditionalCustomAtoms>
T& get(generic::ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...> const& wrapper) {
  try {
    return ::std::visit(
        [](auto& argument) -> T& {
          if constexpr(::std::is_same_v<::std::decay_t<decltype(argument)>,
                                        MovableReferenceWrapper<T>>) {
            return argument.get();
          } else if constexpr(::std::is_same_v<::std::decay_t<decltype(argument)>,
                                               ::std::vector<bool>::reference>) {
            if constexpr(::std::is_same_v<::std::decay_t<T>, ::std::vector<bool>::reference>) {

              return argument;
            }
            throw ::std::bad_variant_access();
          } else if constexpr(boss::utilities::isInstanceOfTemplate<
                                  ::std::decay_t<decltype(argument)>,
                                  MovableReferenceWrapper>::value &&
                              boss::utilities::isInstanceOfTemplate<
                                  ::std::decay_t<decltype(argument.get())>,
                                  ExpressionWithAdditionalCustomAtoms>::value) {
            return ::std::get<T>(argument.get());
          } else {
            throw ::std::bad_variant_access();
          }
        },
        wrapper.getArgument());
  } catch(std::bad_variant_access&) {
    throw formatBOSSBadVariantAccess<T>(wrapper);
  }
}

template <size_t I, bool ConstWrappee, typename... AdditionalCustomAtoms>
constexpr ::std::variant_alternative_t<I, ArgumentWrappeeType<AdditionalCustomAtoms...>>&
get(ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...> const& wrapper) noexcept {
  return ::std::get<I>(wrapper.getArgument());
};

template <typename T, auto ConstWrappee = false, typename... AdditionalCustomAtoms>
T* get_if(ArgumentWrapper<ConstWrappee, AdditionalCustomAtoms...> const* wrapper) {
  return ::std::visit(
      [](auto& argument) -> T* {
        if constexpr(::std::is_same_v<::std::decay_t<decltype(argument)>,
                                      MovableReferenceWrapper<T>>) {
          return &argument.get();
        } else if constexpr(::std::is_same_v<::std::decay_t<decltype(argument)>,
                                             ::std::vector<bool>::reference>) {
          if constexpr(::std::is_same_v<::std::decay_t<T>, ::std::vector<bool>::reference>) {

            return &argument;
          }
          return nullptr;
        } else if constexpr(boss::utilities::isInstanceOfTemplate<
                                ::std::decay_t<decltype(argument)>,
                                MovableReferenceWrapper>::value &&
                            boss::utilities::isInstanceOfTemplate<
                                ::std::decay_t<decltype(argument.get())>,
                                ExpressionWithAdditionalCustomAtoms>::value) {
          return ::std::get_if<T>(argument.get());
        } else {
          return nullptr;
        }
      },
      wrapper->getArgument());
}

template <typename T, typename... AdditionalCustomAtoms>
T* get_if(ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const* wrapper) {
  return ::std::visit(
      [](auto& argument) -> T* {
        if constexpr(::std::is_same_v<::std::decay_t<decltype(argument)>,
                                      MovableReferenceWrapper<T>>) {
          return &argument.get();
        } else if constexpr(::std::is_same_v<::std::decay_t<decltype(argument)>,
                                             ::std::vector<bool>::reference>) {
          if constexpr(::std::is_same_v<::std::decay_t<T>, ::std::vector<bool>::reference>) {

            return &argument;
          }
          return nullptr;
        } else if constexpr(boss::utilities::isInstanceOfTemplate<
                                ::std::decay_t<decltype(argument)>,
                                MovableReferenceWrapper>::value &&
                            boss::utilities::isInstanceOfTemplate<
                                ::std::decay_t<decltype(argument.get())>,
                                ExpressionWithAdditionalCustomAtoms>::value) {
          return ::std::get_if<T>(argument.get());
        } else {
          return nullptr;
        }
      },
      *wrapper);
}

} // namespace generic
using DefaultExpressionSystem = generic::ExtensibleExpressionSystem<>;

using AtomicExpression = DefaultExpressionSystem::AtomicExpression;
template <typename... StaticArgumentTypes>
using ComplexExpressionWithStaticArguments =
    DefaultExpressionSystem::ComplexExpressionWithStaticArguments<StaticArgumentTypes...>;
using ComplexExpression = DefaultExpressionSystem::ComplexExpressionWithStaticArguments<>;
using Expression = DefaultExpressionSystem::Expression;
using ExpressionArguments = DefaultExpressionSystem::ExpressionArguments;
using ExpressionSpanArguments = DefaultExpressionSystem::ExpressionSpanArguments;

using boss::utilities::bad_variant_access; // NOLINT

} // namespace expressions

using expressions::ComplexExpression;
using expressions::ComplexExpressionWithStaticArguments;
using expressions::DefaultExpressionSystem;
using expressions::Expression;
using expressions::ExpressionArguments;
using expressions::Span; // NOLINT
using expressions::Symbol;
using expressions::generic::ExtensibleExpressionSystem; // NOLINT
using expressions::generic::get;                        // NOLINT
} // namespace boss

namespace std {

template <typename... AdditionalCustomAtoms>
struct variant_size<typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
    AdditionalCustomAtoms...>>
    : variant_size<typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
          AdditionalCustomAtoms...>::SuperType> {};

template <typename... AdditionalCustomAtoms>
struct variant_size<const typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
    AdditionalCustomAtoms...>>
    : variant_size<const typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
          AdditionalCustomAtoms...>::SuperType> {};

template <::std::size_t I, typename... AdditionalCustomAtoms>
struct variant_alternative<I, typename boss::expressions::generic::
                                  ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_alternative<I,
                          typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
                              AdditionalCustomAtoms...>::SuperType> {};
template <typename Func, typename... AdditionalCustomAtoms>
decltype(auto) visit(Func&& func,
                     typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
                         AdditionalCustomAtoms...>& e) {
  return visit(::std::forward<Func>(func),
               (typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
                   AdditionalCustomAtoms...>::SuperType&)e);
};
template <typename Func, typename... AdditionalCustomAtoms>
decltype(auto) visit(Func&& func,
                     typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
                         AdditionalCustomAtoms...> const& e) {
  return visit(::std::forward<Func>(func),
               (typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
                   AdditionalCustomAtoms...>::SuperType const&)e);
};
template <typename Func, typename... AdditionalCustomAtoms>
decltype(auto) visit(Func&& func,
                     typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
                         AdditionalCustomAtoms...>&& e) {
  return visit(::std::forward<Func>(func),
               (typename boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
                    AdditionalCustomAtoms...>::SuperType &&)::std::move(e));
};
template <> struct hash<boss::expressions::Symbol> {
  ::std::size_t operator()(boss::expressions::Symbol const& s) const noexcept {
    return ::std::hash<::std::string>{}(s.getName());
  }
};

#ifdef __clang__

#elif __GNUC__
namespace __detail {
namespace __variant {
template <typename... CustomAtoms>
struct _Extra_visit_slot_needed<
    ::std::__detail::__variant::__deduce_visit_result<void>,
    const boss::ExpressionWithAdditionalCustomAtoms<CustomAtoms...>&> // NOLINT
{
  template <typename> struct _Variant_never_valueless : false_type {}; // NOLINT
  static constexpr bool value = false;
};

template <typename... CustomAtoms>
struct _Extra_visit_slot_needed<
    ::std::__detail::__variant::__deduce_visit_result<void>,
    const boss::ExpressionWithAdditionalCustomAtoms<CustomAtoms...>> // NOLINT
{
  template <typename> struct _Variant_never_valueless : false_type {}; // NOLINT
  static constexpr bool value = false;
};

} // namespace __variant
} // namespace __detail
#endif

} // namespace std
