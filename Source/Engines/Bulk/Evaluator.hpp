#pragma once

#include "BatchHelper.hpp"

#include <array>
#include <tuple>
#include <vector>

namespace boss::engines::bulk {

/********************* class Evaluator ************************/

/* to call iteratively a generic lambda function              */
/* from a specifc list of argument Batch types                */
/**************************************************************/

template <typename... BatchTypes> class AllowedBatches {
public:
  using BatchHelper = BatchHelper<BatchTypes...>;
  template <typename Type>
  static constexpr bool includes = ((std::is_same_v<Type, BatchTypes>) || ...);

  static bool isAllowed(Batch const& batch) {
    return (... || (batch.typeId() == UniqueId::forType<BatchTypes>()));
  }
};

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

    template <typename... BatchIn> Batch::ReadablePtr operator()(BatchIn const&... in) const {
      return Batch::ReadablePtr(m_func(in...));
    }

    template <typename... BatchIn> Batch::ReadablePtr operator()(BatchIn&&... in) const {
      return Batch::ReadablePtr(m_func(std::forward<BatchIn>(in)...));
    }

    static constexpr bool isExactType(size_t index, Batch const& batch) {
      return isExactTypeHelper(index, batch, std::make_index_sequence<sizeof...(AllowedBatches)>{});
    }

    template <size_t Index, typename Type> static constexpr bool isExactType() {
      if constexpr(Index < sizeof...(AllowedBatches)) {
        using AllowedBatchTypes = std::tuple_element_t<Index, std::tuple<AllowedBatches...>>;
        return AllowedBatchTypes::template includes<Type>;
      } else {
        using AllowedBatchTypes =
            std::tuple_element_t<sizeof...(AllowedBatches) - 1, std::tuple<AllowedBatches...>>;
        return AllowedBatchTypes::template includes<Type>;
      }
    }

    template <size_t Index, typename Vis>
    static bool visitExactType(Vis&& visitor, Batch const& batch) {
      if constexpr(Index < sizeof...(AllowedBatches)) {
        using AllowedBatchTypes = std::tuple_element_t<Index, std::tuple<AllowedBatches...>>;
        return AllowedBatchTypes::BatchHelper::visit(visitor, batch);
      } else {
        using AllowedBatchTypes =
            std::tuple_element_t<sizeof...(AllowedBatches) - 1, std::tuple<AllowedBatches...>>;
        return AllowedBatchTypes::BatchHelper::visit(visitor, batch);
      }
    };

  private:
    std::string const m_symbol;
    Func m_func;

    template <size_t Index> static constexpr bool isExactType(Batch const& batch) {
      return std::tuple_element_t<Index, std::tuple<AllowedBatches...>>::isAllowed(batch);
    }

    template <size_t... Indices>
    static constexpr bool isExactTypeHelper(size_t index, Batch const& batch,
                                            std::index_sequence<Indices...> /*unused*/) {
      using CheckFuncPtr = bool (*)(Batch const&);
      constexpr std::array<CheckFuncPtr, sizeof...(AllowedBatches)> table = {
          &isExactType<Indices>...};
      if(index >= sizeof...(AllowedBatches)) {
        return table[sizeof...(AllowedBatches) - 1](batch);
      }
      auto& funcIsExactType = table[index]; // NOLINT, bounds are checked just above
      return funcIsExactType(batch);
    }
  }; // class Evaluator

}; // class ForTypes

} // namespace boss::engines::bulk
