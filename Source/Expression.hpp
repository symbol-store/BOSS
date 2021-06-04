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
  template <typename = std::enable_if<sizeof...(AdditionalCustomAtoms) != 0>, typename... T>
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
  ~ExpressionWithAdditionalCustomAtoms() = default;
  ExpressionWithAdditionalCustomAtoms(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;
  ExpressionWithAdditionalCustomAtoms(ExpressionWithAdditionalCustomAtoms const&) noexcept =
      default;
  ExpressionWithAdditionalCustomAtoms&
  operator=(ExpressionWithAdditionalCustomAtoms const&) = default;
  ExpressionWithAdditionalCustomAtoms&
  operator=(ExpressionWithAdditionalCustomAtoms&&) noexcept = default;
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
  template <typename = std::enable_if<sizeof...(AdditionalCustomAtoms) != 0>, typename... T>
  explicit ComplexExpressionWithAdditionalCustomAtoms(
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
bool operator==(boss::Expression const& r1, boss::Expression const& r2);
static bool operator!=(boss::Expression const& r1, boss::Expression const& r2) {
  return !(r1 == r2);
};
