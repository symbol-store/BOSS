#pragma once

#include "BatchVisitDispatcher.hpp"

#include <array>
#include <tuple>
#include <vector>

namespace boss::engines::bulk {

template <typename... BatchTypes> class AllowedBatches {
public:
  using BatchVisitDispatcher = BatchVisitDispatcher<BatchTypes...>;
  template <typename Type>
  static constexpr bool includes = ((std::is_same_v<Type, BatchTypes>) || ...);

  static bool isAllowed(Batch const& batch) {
    return (... || (batch.typeId() == UniqueId::forType<BatchTypes>()));
  }
};

/** The Evaluator class is used to store a generic lambda function
 * which is called to evaluate an operator.
 * The lambda function takes generic argument types,
 * which will be resolved to specific batch types at compile time.
 * The AllowedBatches parameter pack has two functions:
 * - to expand the different combination of argument type at compile time
 * restricting ourselves to only a subset of allowed types
 * - to check the compatibility of the argument batches at run-time
 * through isSupportedType function calls
 */
template <typename... AllowedBatches> class ForTypes {
public:
  template <typename Func> class Evaluator {
  public:
    Evaluator(std::string const& symbol, Func&& func) : m_symbol(symbol), m_func(func) {}
    ~Evaluator() = default;
    Evaluator(Evaluator const&) = default;
    Evaluator(Evaluator&&) noexcept = default;
    Evaluator& operator=(Evaluator const&) = delete;
    Evaluator& operator=(Evaluator&&) = delete;

    std::string const& getSymbol() const { return m_symbol; }

    /** perform the evaluation of the operator.
     * Takes arguments of type inherited from Batch, and following these constraints:
     * - By contract, the types need to be checked using isSupportedType/visitSupportedType,
     * otherwise the lambda function might fail at compile-time or at run-time
     * - The number of arguments should specifically match the operator's ones
     * or it will fail at compile-time. */
    template <typename... BatchIn> Batch::ReadablePtr operator()(BatchIn&&... in) const {
      return Batch::ReadablePtr(m_func(std::forward<BatchIn>(in)...));
    }

    /// Check if the batch type matches one of the expected types for the evaluator's nth argument.
    static bool isSupportedType(size_t argumentIndex, Batch const& batch) {
      // does the evaluator have a type or the batch?
      return isSupportedTypeInternal(argumentIndex, batch,
                                     std::make_index_sequence<sizeof...(AllowedBatches)>{});
    }

    /// Check if the batch type matches one of the expected types for the evaluator's nth argument.
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
    std::string const m_symbol;
    Func m_func;

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
  }; // class Evaluator

}; // class ForTypes

} // namespace boss::engines::bulk
