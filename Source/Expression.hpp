#pragma once
#include "Utilities.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
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
    std::variant<bool, std::int64_t, std::double_t, std::string, Symbol, AdditionalCustomAtoms...>;

template <typename... AdditionalCustomAtoms> class ComplexExpressionWithAdditionalCustomAtoms;

template <typename... AdditionalCustomAtoms>
class ExpressionWithAdditionalCustomAtoms
    : public variant_amend<
          AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>,
          ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>::type {
public:
  using SuperType = typename variant_amend<
      AtomicExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>,
      ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>>::type;

  using SuperType::SuperType;
  explicit ExpressionWithAdditionalCustomAtoms(int32_t v) noexcept
      : ExpressionWithAdditionalCustomAtoms(int64_t(v)) {}
  explicit ExpressionWithAdditionalCustomAtoms(float_t v) noexcept
      : ExpressionWithAdditionalCustomAtoms(double_t(v)) {}
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
    using ComplexExpression = ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
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

template <typename... AdditionalCustomAtoms> class ComplexExpressionWithAdditionalCustomAtoms {
private:
  Symbol head;
  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> arguments;

public:
  explicit ComplexExpressionWithAdditionalCustomAtoms(
      Symbol const& head,
      ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>&& arguments)
      : head(head), arguments(std::move(arguments)){};
  template <typename = std::enable_if<sizeof...(AdditionalCustomAtoms) != 0>, typename... T>
  explicit ComplexExpressionWithAdditionalCustomAtoms(
      ComplexExpressionWithAdditionalCustomAtoms<T...>&& other)
      : head(other.getHead()) {
    arguments.reserve(other.getArguments().size());
    for(auto&& arg : other.getArguments()) {
      arguments.emplace_back(std::move(arg));
    }
  }

  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const&
  getArguments() const {
    return arguments;
  };
  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>& getArguments() {
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
  using ComplexExpression = ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
  using Expression = ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
  using ExpressionArguments =
      ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
};

using DefaultExpressionSystem = ExtensibleExpressionSystem<>;

using AtomicExpression = DefaultExpressionSystem::AtomicExpression;
using ComplexExpression = DefaultExpressionSystem::ComplexExpression;
using Expression = DefaultExpressionSystem::Expression;
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
