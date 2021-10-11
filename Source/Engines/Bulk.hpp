#pragma once
#include "../Engine.hpp"
#include "../Utilities.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
using namespace std;
namespace {                        // https://stackoverflow.com/a/52303687
template <typename> struct tag {}; // <== this one IS literal

template <typename T, typename V> struct get_index;

template <typename T, typename... Ts>
struct get_index<T, std::variant<Ts...>>
    : std::integral_constant<size_t, std::variant<tag<Ts>...>(tag<T>()).index()> {};

template <typename Head, typename... AcceptedTypes>
struct TypeIdentifier
    : public std::integral_constant<size_t,
                                    TypeIdentifier<AcceptedTypes...>::value *
                                            variant_size_v<boss::Expression::SuperType> +
                                        (get_index<Head, boss::Expression::SuperType>::value + 1)> {
};

template <typename Head>
class TypeIdentifier<Head>
    : public std::integral_constant<size_t,
                                    (get_index<Head, boss::Expression::SuperType>::value + 1)> {};

} // namespace

namespace boss::engines::bulk {
class Op {
public:
  virtual boss::Expression operator()(ExpressionArguments const& args) = 0;
  Op() = default;
  Op(Op&&) = default;
  Op(Op const&) = delete;
  Op& operator=(Op&&) = default;
  Op& operator=(Op const&) = delete;
  virtual ~Op() = default;
};

template <template <typename...> typename Subclass,
          typename... AcceptableTypes> // one per argument
class Operator : public Op {
public:
  boss::Expression operator()(ExpressionArguments const& args) override {
    return (*this)(args, std::index_sequence_for<AcceptableTypes...>{});
  }

  template <size_t... I>
  boss::Expression operator()(ExpressionArguments const& args,
                              std::index_sequence<I...> /*unused*/) {
    return ((Subclass<AcceptableTypes...>&)*this)((std::get<AcceptableTypes>(args.at(I)))...);
  };
};

class OperatorDirectory : public std::map<std::pair<std::string, size_t>, std::unique_ptr<Op>> {
  template <template <typename...> typename Operator, typename... Types>
  void emplaceSpecificOperator(std::string const& name, std::tuple<Types...> /*types*/ = {}) {
    map::emplace(std::pair<std::string, size_t>{name, TypeIdentifier<Types...>::value},
                 new Operator<Types...>());
  }

  template <template <typename...> typename Operator, typename... Signatures>
  void emplaceOperatorCategory(std::string const& name,
                               std::variant<Signatures...> /*types*/ = {}) {
    (emplaceSpecificOperator<Operator>(name, Signatures{}), ...);
  }

public:
  template <template <typename...> typename Operator> auto emplace(std::string const& name) {
    emplaceOperatorCategory<Operator>(name, typename Operator<>::ArgumentTypesT{});
  }
};

class Engine : public boss::Engine {
  static OperatorDirectory& getOperatorDirectory();

public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = delete;
  Engine() = default;

  template <template <typename...> typename Op> struct Register {
    explicit Register(char const* name) { Engine::getOperatorDirectory().emplace<Op>(name); }
  };

  boss::Expression evaluate(Expression const& e);
  ~Engine() = default;
};

} // namespace boss::engines::bulk
