#pragma once

#include "ValueBatch.hpp"

#include "../BatchFactory.hpp"
#include "../BatchHelper.hpp"
#include "../SymbolPool.hpp"

#include "../../../Expression.hpp"

namespace boss::engines::bulk {

class SymbolBatch : public ValueBatch<Symbol> {
public:
  using ValueType = Symbol;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<SymbolBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  explicit SymbolBatch(BatchFactory const& factory, size_t count = 0)
      : m_factory(factory), ValueBatch(count) {}
  explicit SymbolBatch(BatchFactory const& factory, Symbol const& symbol)
      : m_factory(factory), ValueBatch(0, symbol) {}
  explicit SymbolBatch(BatchFactory const& factory, Symbol&& symbol)
      : m_factory(factory), ValueBatch(0, std::move(symbol)) {}
  SymbolBatch(BatchFactory const& factory, size_t size, Symbol const& symbol)
      : m_factory(factory), ValueBatch(size, symbol) {}
  SymbolBatch(BatchFactory const& factory, size_t size, Symbol&& symbol)
      : m_factory(factory), ValueBatch(size, std::move(symbol)) {}

  SymbolBatch(BatchFactory const& factory, arrow::ArrayVector&& arrays,
              std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder)
      : m_factory(factory), ValueBatch(std::move(arrays), std::move(arrayBuilder)) {}

  SymbolBatch(SymbolBatch const& other, bool clear = false)
      : m_factory(other.m_factory), ValueBatch(other, clear) {}
  SymbolBatch(SymbolBatch&& other, bool clear = false) noexcept
      : m_factory(other.m_factory), ValueBatch(std::move(other), clear) {}

  ~SymbolBatch() override = default;
  SymbolBatch& operator=(SymbolBatch const& other) = delete;
  SymbolBatch& operator=(SymbolBatch&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsSymbolBatch(clear));
  }
  WritableBatchPtr<ValueBatch> cloneAsValueBatch(bool clear = false) const override {
    return WritableBatchPtr<ValueBatch>(cloneAsSymbolBatch(clear));
  }
  virtual WritableBatchPtr<SymbolBatch> cloneAsSymbolBatch(bool clear = false) const {
    return WritableBatchPtr(new SymbolBatch(*this, clear));
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, SymbolBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsSymbolBatch(clear);
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    outputPtr.reset();
    if(size() == 0) {
      return false;
    }

    // TODO: handle returning multiple array type
    // (for now just assume always a single symbol)

    // make sure to return a non-writable...
    // TODO: cleanup SymbolPool to avoid mistakes
    auto const& batchPtr = DefaultSymbolPool::instance().findSymbol(*begin());
    if(!batchPtr) {
      return false;
    }

    outputPtr = batchPtr;

    return true;
  }

private:
  BatchFactory const& m_factory;
};

} // namespace boss::engines::bulk
