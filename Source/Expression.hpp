#pragma once
#include "Utilities.hpp"
#include <string>
#include <variant>
#include <vector>

namespace boss {
class Symbol {
  std::string name;

public:
  explicit Symbol(std::string const& name) : name(name){};
  std::string const& getName() const { return name; };
};
class CompareSymbolNames {
public:
  bool operator()(Symbol const& lhs, Symbol const& rhs) const {
    return lhs.getName() < rhs.getName();
  }
};

template <typename T, typename... Args> struct variant_amend;

template <typename... Args0, typename... Args1>
struct variant_amend<std::variant<Args0...>, Args1...> {
  using type = std::variant<Args0..., Args1...>;
};

template <typename... AdditionalCustomAtoms>
using AtomicExpressionWithAdditionalCustomAtoms =
    std::variant<bool, int, float, std::string, Symbol, AdditionalCustomAtoms...>;

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
  template <typename... T, typename U = std::tuple<T...>,
            typename = std::enable_if<std::tuple_size<U>::value != 0>>
  ExpressionWithAdditionalCustomAtoms( // NOLINT(hicpp-explicit-conversions)
      ExpressionWithAdditionalCustomAtoms<T...> const& o) noexcept
      : SuperType(std::visit(
            utilities::overload(
                [](ComplexExpressionWithAdditionalCustomAtoms<T...> const& unpacked)
                    -> ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> {
                  return ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>(
                      unpacked);
                },
                [](auto const& unpacked) {
                  return ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>(unpacked);
                }),
            (typename variant_amend<AtomicExpressionWithAdditionalCustomAtoms<T...>,
                                    ComplexExpressionWithAdditionalCustomAtoms<T...>>::type const&)
                o)) {}

  ExpressionWithAdditionalCustomAtoms() = default;
  ~ExpressionWithAdditionalCustomAtoms() = default;
  ExpressionWithAdditionalCustomAtoms(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;
  ExpressionWithAdditionalCustomAtoms(ExpressionWithAdditionalCustomAtoms const&) noexcept =
      default;
  ExpressionWithAdditionalCustomAtoms&
  operator=(ExpressionWithAdditionalCustomAtoms const&) = default;
  ExpressionWithAdditionalCustomAtoms&
  operator=(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;

  bool operator==(ExpressionWithAdditionalCustomAtoms const& other) const {
    using ComplexExpression = ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...>;
    SuperType const& v1 = *this;
    SuperType const& v2 = other;
    if(v1.index() == v2.index()) {
      return std::visit(
          utilities::overload(
              [&v2](ComplexExpression const& r1) {
                auto const& r2Expression = std::get<ComplexExpression>(v2);
                if(r1.getHead().getName() != r2Expression.getHead().getName() ||
                   r1.getArguments().size() != r2Expression.getArguments().size()) {
                  return false;
                }
                for(auto i = 0u; i < r1.getArguments().size(); i++) {
                  if(r1.getArguments()[i] != r2Expression.getArguments()[i]) {
                    return false;
                  }
                }
                return true;
              },
              [&](Symbol const& r1) { return r1.getName() == std::get<Symbol>(v2).getName(); },
              [&](auto r1) { return r1 == std::get<decltype(r1)>(v2); }),
          v1);
    }
    return false;
  }
  bool operator!=(ExpressionWithAdditionalCustomAtoms const& other) const {
    return !(*this == other);
  }
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
      ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const& arguments)
      : head(head), arguments(arguments){};
  template <typename... T, typename U = std::tuple<T...>,
            typename = std::enable_if<std::tuple_size<U>::value != 0>>
  ComplexExpressionWithAdditionalCustomAtoms( // NOLINT(hicpp-explicit-conversions)
      ComplexExpressionWithAdditionalCustomAtoms<T...> const& other)
      : head(other.getHead()), arguments([&other] {
          ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> arguments;
          for(auto const& it : other.getArguments()) {
            arguments.push_back(it);
          }
          return arguments;
        }()) {}
  ExpressionArgumentsWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const&
  getArguments() const {
    return arguments;
  };
  Symbol const& getHead() const { return head; };
  ~ComplexExpressionWithAdditionalCustomAtoms() = default;
  ComplexExpressionWithAdditionalCustomAtoms(
      ComplexExpressionWithAdditionalCustomAtoms&&) noexcept = default;
  ComplexExpressionWithAdditionalCustomAtoms(
      ComplexExpressionWithAdditionalCustomAtoms const&) noexcept = default;
  ComplexExpressionWithAdditionalCustomAtoms&
  operator=(ComplexExpressionWithAdditionalCustomAtoms const&) = default;
  ComplexExpressionWithAdditionalCustomAtoms&
  operator=(ComplexExpressionWithAdditionalCustomAtoms&&) noexcept = default;
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
