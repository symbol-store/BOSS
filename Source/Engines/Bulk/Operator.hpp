#pragma once

#include "Batch/Batch.hpp"
#include "BatchVisitDispatcher.hpp"

namespace boss::engines::bulk {

template <size_t N, typename... AllowedBatches> class Operator {
public:
  class Properties {
  public:
    static size_t constexpr ArgumentCount = N;

    /// Check if the batch type matches one of the expected types for the operator's nth argument.
    static bool isSupportedType(size_t argumentIndex, Batch const& batch) {
      return isSupportedTypeInternal(argumentIndex, batch,
                                     std::make_index_sequence<sizeof...(AllowedBatches)>{});
    }

    /// Check if the batch type matches one of the expected types for the operator's nth argument.
    /// This is a compile-time check alternative.
    template <size_t ArgumentIndex, typename BatchType> static constexpr bool isSupportedType() {
      if constexpr(ArgumentIndex < sizeof...(AllowedBatches)) {
        using AllowedBatchTypes =
            std::tuple_element_t<ArgumentIndex, std::tuple<AllowedBatches...>>;
        return AllowedBatchTypes::template includes<BatchType>;
      } else {
        using AllowedBatchTypes =
            std::tuple_element_t<sizeof...(AllowedBatches) - 1, std::tuple<AllowedBatches...>>;
        return AllowedBatchTypes::template includes<BatchType>;
      }
    }

    /// This function allows to call back the visitor with the batch (casted to the supported type).
    /// If the provided batch isn't supported for the nth argument, the visitor won't be called.
    template <size_t ArgumentIndex, typename Vis>
    static bool visitSupportedType(Vis&& visitor, Batch const& batch) {
      if constexpr(ArgumentIndex < sizeof...(AllowedBatches)) {
        using AllowedBatchTypes =
            std::tuple_element_t<ArgumentIndex, std::tuple<AllowedBatches...>>;
        return AllowedBatchTypes::BatchVisitDispatcher::visit(visitor, batch);
      } else {
        using AllowedBatchTypes =
            std::tuple_element_t<sizeof...(AllowedBatches) - 1, std::tuple<AllowedBatches...>>;
        return AllowedBatchTypes::BatchVisitDispatcher::visit(visitor, batch);
      }
    };

  private:
    template <size_t ArgumentIndex> static constexpr bool isSupportedType(Batch const& batch) {
      return std::tuple_element_t<ArgumentIndex, std::tuple<AllowedBatches...>>::isAllowed(batch);
    }

    template <size_t... Indices>
    static constexpr bool isSupportedTypeInternal(size_t argumentIndex, Batch const& batch,
                                                  std::index_sequence<Indices...> /*unused*/) {
      using CheckFuncPtr = bool (*)(Batch const&);
      constexpr std::array<CheckFuncPtr, sizeof...(AllowedBatches)> table = {
          &isSupportedType<Indices>...};
      if(argumentIndex >= sizeof...(AllowedBatches)) {
        return table[sizeof...(AllowedBatches) - 1](batch);
      }
      auto& funcIsSupportedType = table[argumentIndex]; // NOLINT, bounds are checked just above
      return funcIsSupportedType(batch);
    }
  }; // class Properties
};

/// Helper to define a set of batch types allowed on one of the argument
/// (when allowing more than one type).
template <typename... BatchTypes> class AllowedBatches {
public:
  using BatchVisitDispatcher = BatchVisitDispatcher<BatchTypes...>;
  static bool isAllowed(Batch const& batch) {
    return (... || (batch.typeId() == UniqueId::forType<BatchTypes>()));
  }
};

/// helper to create operator based a list of allowed data types
template <size_t N> class OperatorBuilder {
private:
  template <typename, typename> struct MergeTwoFixedTypes;
  template <typename... Args0, typename... Args1>
  struct MergeTwoFixedTypes<Operator<N, AllowedBatches<Args0...>>,
                            Operator<N, AllowedBatches<Args1...>>> {
    using type = Operator<N, AllowedBatches<Args0...>, AllowedBatches<Args1...>>;
  };
  template <typename, typename> struct MergeTwoAllowedTypes;
  template <typename... Args0, typename... Args1>
  struct MergeTwoAllowedTypes<Operator<N, AllowedBatches<Args0...>>,
                              Operator<N, AllowedBatches<Args1...>>> {
    using type = Operator<N, AllowedBatches<Args0..., Args1...>>;
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
      UsingFixedTypes,
      std::conditional_t<std::is_same_v<Types, Symbol>, Operator<N, AllowedBatches<SymbolBatch>>,
                         std::conditional_t<std::is_same_v<Types, ComplexExpression>,
                                            Operator<N, AllowedBatches<CompoundBatch>>,
                                            Operator<N, AllowedBatches<ValueBatch<Types>>>>>...>::
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
