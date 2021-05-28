#pragma once
#include "Batch/Batch.hpp"
namespace boss::engines::bulk {

/** Not used so far. We can get rid of the base class if we don't need any virtual function
 * or common code. */
class Operator {};

template <typename Func, size_t FuncArgCount, typename... AllowedBatches>
class RegisteredOperator : public Operator {
public:
  explicit RegisteredOperator(Func&& func) : func(func) {}
  ~RegisteredOperator() = default;
  RegisteredOperator(RegisteredOperator const&) = default;
  RegisteredOperator(RegisteredOperator&&) noexcept = default;
  RegisteredOperator& operator=(RegisteredOperator const&) = delete;
  RegisteredOperator& operator=(RegisteredOperator&&) = delete;

  /// calls the evaluation function with specific Batch types as arguments (not just generic Batch)
  template <typename InputBatchTuple, size_t... Indices>
  Batch::ReadablePtr evaluate(InputBatchTuple&& in,
                              std::index_sequence<Indices...> /*unused*/) const {
    return func(std::get<Indices>(std::forward<InputBatchTuple>(in))...);
  }

  class Properties {
  public:
    static size_t constexpr ArgumentCount = FuncArgCount;

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

private:
  Func func;
};
} // namespace boss::engines::bulk
