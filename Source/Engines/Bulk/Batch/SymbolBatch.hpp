#pragma once

#include "RLEBatch.hpp"

#include "../BatchFactory.hpp"
#include "../SymbolPool.hpp"

#include "../../../Expression.hpp"

namespace boss::engines::bulk {

class SymbolBatch : public RLEBatch<Symbol> {
public:
  using ValueType = Symbol; // still pretend to be a Symbol
  static constexpr UniqueId::type UniqueId = UniqueId::forType<SymbolBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  SymbolBatch(size_t count = 0) : RLEBatch(count, Symbol("")) {}
  SymbolBatch(SymbolBatch const& other) : RLEBatch(other) {}
  SymbolBatch(Symbol const& symbol) : RLEBatch(symbol) {}
  SymbolBatch(Symbol&& symbol) : RLEBatch(std::move(symbol)) {}

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(clear ? new SymbolBatch(m_value) : new SymbolBatch(*this));
  }

  BatchPtr evaluate() const override {
    auto& batchPtr = DefaultSymbolPool::instance().findSymbol(m_value);
    if(batchPtr) {
      return batchPtr.get()->clone();
    }
    auto& writablePtr = WritableBatchPool::instance().findSymbol(m_value);
    if(writablePtr) {
      return writablePtr.get()->clone();
    }
    return this->clone();
  }
};

} // namespace boss::engines::bulk
