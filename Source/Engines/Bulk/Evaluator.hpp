#pragma once

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
    Evaluator(Evaluator const&) = default;

    std::string const& getSymbol() const { return m_symbol; }

    template <typename... BatchIn> BatchPtr operator()(BatchIn const&... in) const {
      return m_func(in...);
    }

    static constexpr bool IsAllowedType(UniqueId::type typeId) {
      return ((typeId == UniqueId::forType<Types>()) || ...);
    }

    template <typename Type> static constexpr bool IsAllowedType() {
      return ((std::is_same_v<Type, Types>) || ...);
    }

    template <typename... Ts> static constexpr bool AreAllowedTypes() {
      return (IsAllowedType<Ts>() && ...);
    }

    static constexpr bool IsExactType(size_t index, UniqueId::type typeId) {
      return IsExactTypeHelper(index, typeId, std::make_index_sequence<sizeof...(Types)>{});
    }

    template <size_t Index, typename Type> static constexpr bool IsExactType() {
      return std::is_same_v<Type, std::tuple_element_t<Index, std::tuple<Types...>>>;
    }

    template <typename... Ts> static constexpr bool AreExactTypes() {
      return std::is_same_v<std::tuple<Types...>, std::tuple<Ts...>>;
    }

    template <typename Vis> static bool VisitAllowedTypes(Vis&& visitor, Batch& batch) {
      return VisitHelper<Vis>(visitor, batch, SupportedTypeList{});
    }

    template <size_t Index, typename Vis> static bool VisitExactType(Vis&& visitor, Batch& batch) {
      using BatchType = std::tuple_element_t<Index, std::tuple<Types...>>;
      return VisitHelper<Vis>(visitor, batch, TypeList<BatchType>{});
    };

  private:
    std::string const m_symbol;
    Func m_func;

    template <typename...> struct TypeList {};
    using SupportedTypeList = TypeList<Types...>;

    template <size_t Index> static constexpr bool IsExactType(UniqueId::type typeId) {
      return typeId == UniqueId::forType<std::tuple_element_t<Index, std::tuple<Types...>>>();
    }

    template <size_t... Indices>
    static constexpr bool IsExactTypeHelper(size_t index, UniqueId::type typeId,
                                            std::index_sequence<Indices...>) {
      if(index < sizeof...(Types)) {
        using CheckFuncPtr = bool (*)(UniqueId::type);
        constexpr CheckFuncPtr table[sizeof...(Types)] = {&IsExactType<Indices>...};
        return table[index](typeId);
      } else {
        return false;
      }
    }

    template <typename Vis, template <typename...> typename BatchTypeList, typename... BatchType>
    static bool VisitHelper(Vis& visitor, Batch& batch, BatchTypeList<BatchType...>) {
      return (... || VisitHelper<std::decay_t<Vis>, BatchType>(visitor, batch));
    }

    template <typename Vis, typename BatchType>
    static bool VisitHelper(Vis& visitor, Batch& batch) {
      if(batch.typeId() == UniqueId::forType<BatchType>()) {
        visitor(*static_cast<BatchType*>(&batch));
        return true;
      }
      return false;
    }
  }; // class Evaluator

}; // class ForTypes

} // namespace boss::engines::bulk
