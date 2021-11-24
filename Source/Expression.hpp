#pragma once
#include "Utilities.hpp"
#include <functional>
#include <string>
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

template <typename... AdditionalCustomAtoms>
using AtomicExpressionWithAdditionalCustomAtoms =
    std::variant<bool, int, float, std::string, Symbol, AdditionalCustomAtoms...>;

template <typename StaticArgumentsTuple, typename... AdditionalCustomAtoms>
class ComplexExpressionWithAdditionalCustomAtoms;

template <typename StaticArgumentsTuple, typename... AdditionalCustomAtoms>
class ExpressionWithAdditionalCustomAtoms
    : public variant_amend<AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>,
                           ComplexExpressionWithAdditionalCustomAtoms<
                               StaticArgumentsTuple, AdditionalCustomAtoms...>>::type {
public:
  using SuperType =
      typename variant_amend<AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>,
                             ComplexExpressionWithAdditionalCustomAtoms<
                                 StaticArgumentsTuple, AdditionalCustomAtoms...>>::type;

  using SuperType::SuperType;
  template <typename = std::enable_if<sizeof...(AdditionalCustomAtoms) != 0>, typename... T>
  ExpressionWithAdditionalCustomAtoms( // NOLINT(hicpp-explicit-conversions)
      ExpressionWithAdditionalCustomAtoms<T...>&& o) noexcept
      : SuperType(std::visit(
            utilities::overload(
                [](ComplexExpressionWithAdditionalCustomAtoms<T...>&& unpacked)
                    -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
                  return ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>(
                      std::forward<decltype(unpacked)>(unpacked));
                },
                [](auto&& unpacked) {
                  return ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>(
                      std::forward<decltype(unpacked)>(unpacked));
                }),
            (typename variant_amend<AtomicExpressionWithAdditionalCustomAtoms<T...>,
                                    ComplexExpressionWithAdditionalCustomAtoms<T...>>::type &&)
                std::move(o))) {}
  ~ExpressionWithAdditionalCustomAtoms() = default;
  ExpressionWithAdditionalCustomAtoms(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;
  ExpressionWithAdditionalCustomAtoms&
  operator=(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;

  template <typename T>
  std::enable_if_t<!std::is_same_v<T, ExpressionWithAdditionalCustomAtoms>, bool>
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
        ComplexExpressionWithAdditionalCustomAtoms<StaticArgumentsTuple, AdditionalCustomAtoms...>;
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

template <typename StaticArgumentsTuple, typename... AdditionalCustomAtoms>
using ExpressionArgumentsWithAdditionalCustomAtoms = std::vector<
    ExpressionWithAdditionalCustomAtoms<StaticArgumentsTuple, AdditionalCustomAtoms...>>;

template <typename StaticArgumentsTuple, typename... AdditionalCustomAtoms>
class ComplexExpressionWithAdditionalCustomAtoms {
private:
  Symbol head;
  StaticArgumentsTuple staticArguments{};
  ExpressionArgumentsWithAdditionalCustomAtoms<StaticArgumentsTuple, AdditionalCustomAtoms...>
      arguments{};

public:
  template <size_t... I>
  static StaticArgumentsTuple
  convertToTuple(ExpressionArgumentsWithAdditionalCustomAtoms<StaticArgumentsTuple,
                                                              AdditionalCustomAtoms...>& arguments,
                 std::index_sequence<I...> /*unused*/) {
    return {move((arguments).at(I))...};
  }
  explicit ComplexExpressionWithAdditionalCustomAtoms(
      Symbol const& head,
      ExpressionArgumentsWithAdditionalCustomAtoms<StaticArgumentsTuple, AdditionalCustomAtoms...>&&
          arguments)
      : head(head),
        staticArguments(convertToTuple(
            arguments, std::make_index_sequence<std::tuple_size<StaticArgumentsTuple>::value>())),
        arguments(std::move_iterator(
                      next(begin(arguments), std::tuple_size<StaticArgumentsTuple>::value)),
                  std::move_iterator(end(arguments))){};
  template <typename = std::enable_if<sizeof...(AdditionalCustomAtoms) != 0>, typename... T>
  explicit ComplexExpressionWithAdditionalCustomAtoms(
      ComplexExpressionWithAdditionalCustomAtoms<T...>&& other)
      : head(other.getHead()) {
    arguments.reserve(other.getArguments().size());
    for(auto&& arg : other.getArguments()) {
      arguments.emplace_back(std::move(arg));
    }
  }

  ExpressionArgumentsWithAdditionalCustomAtoms<StaticArgumentsTuple,
                                               AdditionalCustomAtoms...> const&
  getArguments() const {
    return arguments;
  };
  ExpressionArgumentsWithAdditionalCustomAtoms<StaticArgumentsTuple, AdditionalCustomAtoms...>&
  getArguments() {
    return arguments;
  };
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
    ExpressionArgumentsWithAdditionalCustomAtoms<StaticArgumentsTuple, AdditionalCustomAtoms...>
        copiedArgs;
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
  template <typename... StaticArgumentTypes>
  using ExpressionWithStaticArguments =
      ExpressionWithAdditionalCustomAtoms<std::tuple<StaticArgumentTypes...>,
                                          AdditionalCustomAtoms...>;
  using Expression = ExpressionWithStaticArguments<>;
  template <typename... StaticArgumentTypes>
  using ExpressionArgumentsWithStaticArguments =
      ExpressionArgumentsWithAdditionalCustomAtoms<std::tuple<StaticArgumentTypes...>,
                                                   AdditionalCustomAtoms...>;
  using ExpressionArguments = ExpressionArgumentsWithStaticArguments<>;
};

using DefaultExpressionSystem = ExtensibleExpressionSystem<>;

using AtomicExpression = DefaultExpressionSystem::AtomicExpression;
template <typename... StaticArgumentTypes>
using ComplexExpressionWithStaticArguments =
    DefaultExpressionSystem::ComplexExpressionWithStaticArguments<StaticArgumentTypes...>;
using ComplexExpression = DefaultExpressionSystem::ComplexExpressionWithStaticArguments<>;
template <typename... StaticArgumentTypes>
using ExpressionWithStaticArguments =
    DefaultExpressionSystem::ExpressionWithStaticArguments<StaticArgumentTypes...>;
using Expression = ExpressionWithStaticArguments<>;
using ExpressionArguments = DefaultExpressionSystem::ExpressionArguments;

} // namespace boss

namespace std {
template <typename... AdditionalCustomAtoms>
struct variant_size<typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_size<
          typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>::SuperType> {
};
template <std::size_t I, typename... AdditionalCustomAtoms>
struct variant_alternative<
    I, typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>
    : variant_alternative<I, typename boss::ExpressionWithAdditionalCustomAtoms<
                                 AdditionalCustomAtoms...>::SuperType> {};
template <typename Func, typename... AdditionalCustomAtoms>
auto visit(Func&& func,
           typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& e) {
  return visit(
      std::forward<Func>(func),
      (typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>::SuperType&)e);
};
template <typename Func, typename... AdditionalCustomAtoms>
auto visit(Func&& func,
           typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const& e) {
  return visit(std::forward<Func>(func), (typename boss::ExpressionWithAdditionalCustomAtoms<
                                             AdditionalCustomAtoms...>::SuperType const&)e);
};
template <typename Func, typename... AdditionalCustomAtoms>
auto visit(Func&& func,
           typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&& e) {
  return visit(
      std::forward<Func>(func),
      (typename boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>::SuperType &&)
          std::move(e));
};
} // namespace std

template <> struct std::hash<boss::Symbol> {
  std::size_t operator()(boss::Symbol const& s) const noexcept {
    return std::hash<std::string>{}(s.getName());
  }
};
