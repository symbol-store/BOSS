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

template <typename... Types> class ForTypes {
public:
  template <typename Func> class Evaluator {
  public:
    Evaluator(std::string const& symbol, Func const& func) : m_symbol(symbol), m_func(func) {}
    Evaluator(std::string const& symbol, Func&& func) : m_symbol(symbol), m_func(func) {}
    ~Evaluator() = default;
    Evaluator(Evaluator const&) = default;
    Evaluator(Evaluator&&) noexcept = default;
    Evaluator& operator=(Evaluator const&) = delete;
    Evaluator& operator=(Evaluator&&) = delete;

    std::string const& getSymbol() const { return m_symbol; }

    template <typename... BatchIn> BatchPtr operator()(BatchIn const&... in) const {
      return m_func(in...);
    }

    static constexpr bool isAllowedType(Batch const& batch) {
      return ((batch.typeId() == UniqueId::forType<Types>()) || ...) ||
             ((batch.baseId() == UniqueId::forType<Types>()) || ...);
    }

    template <typename Type> static constexpr bool isAllowedType() {
      return ((std::is_same_v<Type, Types>) || ...);
    }

    template <typename... Ts> static constexpr bool areAllowedTypes() {
      return (isAllowedType<Ts>() && ...);
    }

    static constexpr bool isExactType(size_t index, Batch const& batch) {
      return isExactTypeHelper(index, batch, std::make_index_sequence<sizeof...(Types)>{});
    }

    template <size_t Index, typename Type> static constexpr bool isExactType() {
      return std::is_same_v<Type, std::tuple_element_t<Index, std::tuple<Types...>>>;
    }

    template <typename... Ts> static constexpr bool areExactTypes() {
      return std::is_same_v<std::tuple<Types...>, std::tuple<Ts...>>;
    }

    template <typename Vis> static bool visitAllowedTypes(Vis&& visitor, Batch& batch) {
      return BatchHelper<Types...>::visit(visitor, batch);
    }

    template <size_t Index, typename Vis> static bool visitExactType(Vis&& visitor, Batch& batch) {
      using BatchType = std::tuple_element_t<Index, std::tuple<Types...>>;
      return BatchHelper<BatchType>::visit(visitor, batch);
    };

    template <typename Vis> static bool visitAllowedTypes(Vis&& visitor, BatchPtr& batchPtr) {
      return BatchHelper<Types...>::visit(visitor, batchPtr);
    }

    template <size_t Index, typename Vis>
    static bool visitExactType(Vis&& visitor, BatchPtr& batchPtr) {
      using BatchType = std::tuple_element_t<Index, std::tuple<Types...>>;
      return BatchHelper<BatchType>::visit(visitor, batchPtr);
    };

  private:
    std::string const m_symbol;
    Func m_func;

    template <size_t Index> static constexpr bool isExactType(Batch const& batch) {
      return (batch.typeId() ==
              UniqueId::forType<std::tuple_element_t<Index, std::tuple<Types...>>>()) ||
             (batch.baseId() ==
              UniqueId::forType<std::tuple_element_t<Index, std::tuple<Types...>>>());
    }

    template <size_t... Indices>
    static constexpr bool isExactTypeHelper(size_t index, Batch const& batch,
                                            std::index_sequence<Indices...> /*unused*/) {
      if(index < sizeof...(Types)) {
        using CheckFuncPtr = bool (*)(Batch const&);
        constexpr std::array<CheckFuncPtr, sizeof...(Types)> table = {&isExactType<Indices>...};
        auto& funcIsExactType = table[index]; // NOLINT, bounds are checked just above
        return funcIsExactType(batch);
      }
      return false;
    }
  }; // class Evaluator

}; // class ForTypes

} // namespace boss::engines::bulk
