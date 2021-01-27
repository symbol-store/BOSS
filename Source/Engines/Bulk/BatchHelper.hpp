#pragma once

#include "Batch/Batch.hpp"

namespace boss::engines::bulk {

template <typename... BatchTypes> class BatchHelper {
public:
  template <typename Func> static bool visit(Func&& func, Batch& batch) {
    return visit(func, batch, BatchTypeList{});
  }
  template <typename Func> static bool visit(Func&& func, Batch const& batch) {
    return visit(func, batch, BatchTypeList{});
  }

private:
  template <typename...> struct TypeList {};
  using BatchTypeList = TypeList<BatchTypes...>;

  template <typename Func, template <typename...> typename List, typename... BatchType>
  static bool visit(Func& func, Batch& batch, List<BatchType...>) {
    return (... || visit<std::decay_t<Func>, BatchType>(func, batch)) ||
           (... || visitBase<std::decay_t<Func>, BatchType>(func, batch));
  }
  template <typename Func, template <typename...> typename List, typename... BatchType>
  static bool visit(Func& func, Batch const& batch, List<BatchType...>) {
    return (... || visit<std::decay_t<Func>, BatchType>(func, batch)) ||
           (... || visitBase<std::decay_t<Func>, BatchType>(func, batch));
  }

  template <typename Func, typename BatchType> static bool visit(Func& func, Batch& batch) {
    if(batch.typeId() == UniqueId::forType<BatchType>()) {
      func(*static_cast<BatchType*>(&batch));
      return true;
    }
    return false;
  }
  template <typename Func, typename BatchType> static bool visit(Func& func, Batch const& batch) {
    if(batch.typeId() == UniqueId::forType<BatchType>()) {
      func(*static_cast<BatchType const*>(&batch));
      return true;
    }
    return false;
  }
  template <typename Func, typename BatchType> static bool visitBase(Func& func, Batch& batch) {
    if(batch.baseId() == UniqueId::forType<BatchType>()) {
      func(*static_cast<BatchType*>(&batch));
      return true;
    }
    return false;
  }
  template <typename Func, typename BatchType>
  static bool visitBase(Func& func, Batch const& batch) {
    if(batch.baseId() == UniqueId::forType<BatchType>()) {
      func(*static_cast<BatchType const*>(&batch));
      return true;
    }
    return false;
  }
};

} // namespace boss::engines::bulk
