#pragma once

#include "ExpressionVisitDispatcher.hpp"

namespace boss::engines::bulk {

template <bool HoldAllArguments, bool MaxArgumentEvaluation, typename... AllowedArguments>
class OperatorWithProperties {
public:
  class Properties {
  public:
    static size_t constexpr ParameterCount = sizeof...(AllowedArguments);
    static bool constexpr maxArgumentEvaluation = MaxArgumentEvaluation;
    static bool constexpr holdAllArguments = HoldAllArguments;

    /// Check if the argument type matches one of the expected types for the operator's nth
    /// argument.
    static bool isSupportedType(size_t argumentIndex, BulkExpression const& expression) {
      return isSupportedTypeInternal(argumentIndex, expression,
                                     std::make_index_sequence<sizeof...(AllowedArguments)>{});
    }

    /// Check if the argument type matches one of the expected types for the operator's nth
    /// argument. This is a compile-time check alternative.
    template <size_t ArgumentIndex, typename ArgumentType> static constexpr bool isSupportedType() {
      if constexpr(ArgumentIndex < sizeof...(AllowedArguments)) {
        using AllowedArgumentTypes =
            std::tuple_element_t<ArgumentIndex, std::tuple<AllowedArguments...>>;
        return AllowedArgumentTypes::template includes<ArgumentType>;
      } else {
        using AllowedArgumentTypes =
            std::tuple_element_t<sizeof...(AllowedArguments) - 1, std::tuple<AllowedArguments...>>;
        return AllowedArgumentTypes::template includes<ArgumentType>;
      }
    }

    /// This function allows to call back the visitor with the argument
    /// (cast to the supported type).
    /// If the provided argument isn't supported as nth argument,
    /// the visitor won't be called.
    template <size_t ArgumentIndex, typename Vis>
    static bool visitSupportedType(Vis&& visitor, BulkExpression const& expression) {
      if constexpr(ArgumentIndex < sizeof...(AllowedArguments)) {
        using AllowedArgumentTypes =
            std::tuple_element_t<ArgumentIndex, std::tuple<AllowedArguments...>>;
        return AllowedArgumentTypes::ExpressionVisitDispatcher::visit(visitor, expression);
      } else {
        using AllowedArgumentTypes =
            std::tuple_element_t<sizeof...(AllowedArguments) - 1, std::tuple<AllowedArguments...>>;
        return AllowedArgumentTypes::ExpressionVisitDispatcher::visit(visitor, expression);
      }
    };

  private:
    template <size_t ArgumentIndex>
    static constexpr bool isSupportedType(BulkExpression const& expression) {
      return std::tuple_element_t<ArgumentIndex, std::tuple<AllowedArguments...>>::isAllowed(
          expression);
    }

    template <size_t... Indices>
    static constexpr bool isSupportedTypeInternal(size_t argumentIndex,
                                                  BulkExpression const& expression,
                                                  std::index_sequence<Indices...> /*unused*/) {
      using CheckFuncPtr = bool (*)(BulkExpression const&);
      constexpr std::array<CheckFuncPtr, sizeof...(AllowedArguments)> table = {
          &isSupportedType<Indices>...};
      if(argumentIndex >= sizeof...(AllowedArguments)) {
        return table[sizeof...(AllowedArguments) - 1](expression);
      }
      auto& funcIsSupportedType = table[argumentIndex]; // NOLINT, bounds are checked just above
      return funcIsSupportedType(expression);
    }
  }; // class Properties
};

template <typename... AllowedArguments>
using OperatorWithMaxEvaluation = OperatorWithProperties<false, true, AllowedArguments...>;
template <typename... AllowedArguments>
using OperatorWithoutMaxEvaluation = OperatorWithProperties<false, false, AllowedArguments...>;
template <typename... AllowedArguments>
using OperatorHoldAllArguments = OperatorWithProperties<true, false, AllowedArguments...>;
template <typename... AllowedArguments>
using Operator = OperatorWithMaxEvaluation<AllowedArguments...>;

/// Helper to define a specific head symbol only allowed on one of the argument
template <char const* name> class CompileTimeSymbol {
public:
  using ArgumentType = BulkComplexExpression;

  static bool isMatching(BulkExpression const& expression) {
    auto const* complexExpression = std::get_if<BulkComplexExpression>(&expression);
    return complexExpression != nullptr &&
           complexExpression->getHead().getName() ==
               name; // NOLINT, std::string function safe to take a char const*
  }
};
template <class T> struct isCompileTimeSymbol { static bool const value = false; };
template <char const* name> struct isCompileTimeSymbol<CompileTimeSymbol<name>> {
  static bool const value = true;
};

/// Helper to define a set of expression types allowed on one of the argument
template <typename... ArgumentTypes> class AllowedArguments {
private:
  template <typename ArgumentType> static bool check(BulkExpression const& expression) {
    if constexpr(isCompileTimeSymbol<ArgumentType>::value) {
      return ArgumentType::isMatching(expression);
    } else {
      return std::holds_alternative<ArgumentType>(expression);
    }
  }

  template <typename T, typename = void> class ExpressionType {
  public:
    using type = T;
  };

  template <typename T> class ExpressionType<T, std::void_t<typename T::ArgumentType>> {
  public:
    using type = typename T::ArgumentType;
  };

public:
  using ExpressionVisitDispatcher =
      ExpressionVisitDispatcher<typename ExpressionType<ArgumentTypes>::type...>;
  static bool isAllowed(BulkExpression const& expression) {
    return (... || (check<ArgumentTypes>(expression)));
  }
};

/// helper to create an operator based on a list of allowed data types
template <size_t N> class OperatorBuilder {
private:
  template <typename, typename> struct ConcatenateTwoArguments;
  template <typename... Args0, typename... Args1>
  struct ConcatenateTwoArguments<OperatorWithProperties<false, true, AllowedArguments<Args0...>>,
                                 OperatorWithProperties<false, true, AllowedArguments<Args1...>>> {
    using type =
        OperatorWithProperties<false, true, AllowedArguments<Args0...>, AllowedArguments<Args1...>>;
  };
  template <size_t N1, typename OperatorImpl> struct GenerateFullOperator {
    using type = typename ConcatenateTwoArguments<
        OperatorImpl, typename GenerateFullOperator<N1 - 1, OperatorImpl>::type>::type;
  };
  template <typename BaseOperator> struct GenerateFullOperator<1, BaseOperator> {
    using type = BaseOperator;
  };
  template <typename... Types>
  using FromElementTypesToAllowedTypes = typename GenerateFullOperator<
      N, OperatorWithProperties<
             false, true,
             AllowedArguments<
                 Types...,
                 std::conditional_t<std::disjunction_v<std::is_same<Types, ComplexExpression>,
                                                       std::is_same<Types, BulkComplexExpression>>,
                                    std::shared_ptr<CompoundArray>,
                                    std::shared_ptr<ValueArray<Types>>>...>>>::type;

  template <typename... Types> struct ForTypes {
    using type = FromElementTypesToAllowedTypes<Types...>;
  };

public:
  template <typename... Types> using OperatorForTypes = typename ForTypes<Types...>::type;
};

} // namespace boss::engines::bulk
