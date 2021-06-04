#pragma once

#include "Batch/Batch.hpp"
#include "BatchVisitDispatcher.hpp"

namespace boss::engines::bulk {

template <size_t N, typename... AllowedArguments> class Operator {
public:
  class Properties {
  public:
    static size_t constexpr ArgumentCount = N;

    /// Check if the batch type matches one of the expected types for the operator's nth argument.
    static bool isSupportedType(size_t argumentIndex, BulkExpression const& expression) {
      return isSupportedTypeInternal(argumentIndex, expression,
                                     std::make_index_sequence<sizeof...(AllowedArguments)>{});
    }

    /// Check if the batch type matches one of the expected types for the operator's nth argument.
    /// This is a compile-time check alternative.
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

    /// This function allows to call back the visitor with the batch (casted to the supported type).
    /// If the provided batch isn't supported for the nth argument, the visitor won't be called.
    template <size_t ArgumentIndex, typename Vis>
    static bool visitSupportedType(Vis&& visitor, BulkExpression const& expression) {
      if constexpr(ArgumentIndex < sizeof...(AllowedArguments)) {
        using AllowedArgumentTypes =
            std::tuple_element_t<ArgumentIndex, std::tuple<AllowedArguments...>>;
        return AllowedArgumentTypes::BatchVisitDispatcher::visit(visitor, expression);
      } else {
        using AllowedArgumentTypes =
            std::tuple_element_t<sizeof...(AllowedArguments) - 1, std::tuple<AllowedArguments...>>;
        return AllowedArgumentTypes::BatchVisitDispatcher::visit(visitor, expression);
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

/// Helper to define a set of batch types allowed on one of the argument
/// (when allowing more than one type).
template <typename... ArgumentTypes> class AllowedArguments {
public:
  using BatchVisitDispatcher = BatchVisitDispatcher<ArgumentTypes...>;
  static bool isAllowed(BulkExpression const& expression) {
    return (... || (std::holds_alternative<ArgumentTypes>(expression)));
  }
};

/// helper to create operator based a list of allowed data types
template <size_t N> class OperatorBuilder {
private:
  template <typename, typename> struct MergeTwoFixedTypes;
  template <typename... Args0, typename... Args1>
  struct MergeTwoFixedTypes<Operator<N, AllowedArguments<Args0...>>,
                            Operator<N, AllowedArguments<Args1...>>> {
    using type = Operator<N, AllowedArguments<Args0...>, AllowedArguments<Args1...>>;
  };
  template <typename, typename> struct MergeTwoAllowedTypes;
  template <typename... Args0, typename... Args1>
  struct MergeTwoAllowedTypes<Operator<N, AllowedArguments<Args0...>>,
                              Operator<N, AllowedArguments<Args1...>>> {
    using type = Operator<N, AllowedArguments<Args0..., Args1...>>;
  };
  template <bool, typename...> struct MergeAllowedTypes;
  template <bool UsingFixedTypes, typename FirstAllowedType>
  struct MergeAllowedTypes<UsingFixedTypes, FirstAllowedType> {
    using type = FirstAllowedType;
  };
  template <bool UsingFixedTypes, typename FirstAllowedType, typename... OtherAllowedTypes>
  struct MergeAllowedTypes<UsingFixedTypes, FirstAllowedType, OtherAllowedTypes...> {
    using type = std::conditional_t<
        UsingFixedTypes,
        typename MergeTwoFixedTypes<
            FirstAllowedType,
            typename MergeAllowedTypes<UsingFixedTypes, OtherAllowedTypes...>::type>::type,
        typename MergeTwoAllowedTypes<
            FirstAllowedType,
            typename MergeAllowedTypes<UsingFixedTypes, OtherAllowedTypes...>::type>::type>;
  };
  template <bool UsingFixedTypes, typename... Types>
  using FromElementTypesToAllowedTypes = typename MergeAllowedTypes<
      UsingFixedTypes, Operator<N, AllowedArguments<Types...>>,
      std::conditional_t<std::disjunction_v<std::is_same<Types, ComplexExpression>,
                                            std::is_same<Types, BulkComplexExpression>>,
                         Operator<N, AllowedArguments<std::shared_ptr<CompoundArray>>>,
                         Operator<N, AllowedArguments<std::shared_ptr<ValueArray<Types>>>>>...>::
      type;

  template <typename... Types> struct ForTypes {
    using type = FromElementTypesToAllowedTypes<false, Types...>;
  };

  template <typename... Types> struct ForTypesInOrder {
    using type = FromElementTypesToAllowedTypes<true, Types...>;
  };

public:
  template <typename... Types> using OperatorForTypes = typename ForTypes<Types...>::type;
  template <typename... Types>
  using OperatorForTypesInOrder = typename ForTypesInOrder<Types...>::type;
};

} // namespace boss::engines::bulk
