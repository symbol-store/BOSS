#pragma once

#include "RLEBatch.hpp"

#include "../BatchFactory.hpp"
#include "../SymbolPool.hpp"

#include "../../../Expression.hpp"

namespace boss::engines::bulk {

class SymbolBatch : public RLEBatch<Symbol> {
public:
  using ValueType = Symbol;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<SymbolBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  explicit SymbolBatch(size_t count = 0) : RLEBatch(count, Symbol("")) {}
  explicit SymbolBatch(Symbol const& symbol) : RLEBatch(symbol) {}
  explicit SymbolBatch(Symbol&& symbol) : RLEBatch(std::move(symbol)) {}
  SymbolBatch(size_t size, Symbol const& symbol) : RLEBatch(size, symbol) {}
  SymbolBatch(size_t size, Symbol&& symbol) : RLEBatch(size, std::move(symbol)) {}

  SymbolBatch(SymbolBatch const& other) = default;
  SymbolBatch(SymbolBatch&& other) noexcept = default;

  ~SymbolBatch() override = default;
  SymbolBatch& operator=(SymbolBatch const& other) = delete;
  SymbolBatch& operator=(SymbolBatch&& other) = delete;

  BatchPtr clone(bool clear = false) const override { return cloneAsSymbolBatch(clear); }

  using RLEBatch::RLEBatchPtr;
  RLEBatchPtr cloneAsRLEBatch(bool clear = false) const override {
    return cloneAsSymbolBatch(clear);
  }

  using SymbolBatchPtr = std::unique_ptr<SymbolBatch>;
  SymbolBatchPtr cloneAsSymbolBatch(bool clear = false) const {
    return SymbolBatchPtr(clear ? new SymbolBatch(*begin()) : new SymbolBatch(*this));
  }

  BatchPtr evaluate() const override {
    auto& batchPtr = DefaultSymbolPool::instance().findSymbol(*begin());
    if(batchPtr) {
      return batchPtr->clone();
    }
    auto& writablePtr = WritableBatchPool::instance().findSymbol(*begin());
    if(writablePtr) {
      return writablePtr->clone();
    }
    return this->clone();
  }
};

} // namespace boss::engines::bulk
