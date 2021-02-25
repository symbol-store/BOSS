#pragma once

#include "Batch/Batch.hpp"

namespace boss::engines::bulk {

template <typename... BatchTypes> class BatchHelper {
public:
  template <typename Func> static bool visit(Func&& func, Batch& batch) {
    return visit(std::forward<Func>(func), batch, BatchTypeList{});
  }
  template <typename Func> static bool visit(Func&& func, Batch const& batch) {
    return visit(std::forward<Func>(func), batch, BatchTypeList{});
  }
  template <typename Func> static bool visit(Func&& func, Batch&& batch) {
    return visit(std::forward<Func>(func), std::forward<Batch>(batch), BatchTypeList{});
  }

  template <typename Func, template <typename...> typename List, typename... BatchType>
  static bool visit(Func&& func, Batch& batch, List<BatchType...> /*unused*/) {
    return (... || visit<std::decay_t<Func>, BatchType>(func, batch));
  }
  template <typename Func, template <typename...> typename List, typename... BatchType>
  static bool visit(Func&& func, Batch const& batch, List<BatchType...> /*unused*/) {
    return (... || visit<std::decay_t<Func>, BatchType>(func, batch));
  }
  template <typename Func, template <typename...> typename List, typename... BatchType>
  static bool visit(Func&& func, Batch&& batch, List<BatchType...> /*unused*/) {
    return (... || visit<std::decay_t<Func>, BatchType>(func, std::forward<BatchType>(batch)));
  }

private:
  template <typename...> struct TypeList {};
  using BatchTypeList = TypeList<BatchTypes...>;

  template <typename Func, typename BatchType> static bool visit(Func& func, Batch& batch) {
    if(batch.typeId() == UniqueId::forType<BatchType>()) {
      auto& specificBatch = *static_cast<BatchType*>(&batch);
      func(specificBatch);
      return true;
    }
    return false;
  }
  template <typename Func, typename BatchType> static bool visit(Func& func, Batch const& batch) {
    if(batch.typeId() == UniqueId::forType<BatchType>()) {
      auto const& specificBatch = *static_cast<BatchType const*>(&batch);
      func(specificBatch);
      return true;
    }
    return false;
  }
  template <typename Func, typename BatchType> static bool visit(Func& func, Batch&& batch) {
    if(batch.typeId() == UniqueId::forType<BatchType>()) {
      func(std::forward(static_cast<BatchType&&>(batch)));
      return true;
    }
    return false;
  }
};

} // namespace boss::engines::bulk
