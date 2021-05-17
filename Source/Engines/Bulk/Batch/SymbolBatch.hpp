#pragma once

#include "ValueBatch.hpp"

#include "../BatchFactory.hpp"
#include "../BatchHelper.hpp"
#include "../SymbolPool.hpp"

#include "../../../Expression.hpp"

namespace boss::engines::bulk {

/** SymbolBatch has almost the same features as a ValueBatch<Symbol>.
 * The only difference is the evaluate function which can retrieve batches stored from the symbol.
 */
class SymbolBatch : public ValueBatch<Symbol> {
  // what is special about a symbolbatch with respect to a ValueBatch<Symbol>?
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

  Batch* clone(bool clear = false) const override { return cloneAsSymbolBatch(clear); }

  ValueBatch* cloneAsValueBatch(bool clear = false) const override {
    return cloneAsSymbolBatch(clear);
  }
  virtual SymbolBatch* cloneAsSymbolBatch(bool clear = false) const {
    return new SymbolBatch(*this, clear);
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, SymbolBatch>, int> = 0>
  BatchType* cloneAs(bool clear = false) const {
    return cloneAsSymbolBatch(clear);
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    outputPtr.reset();
    if(size() == 0) {
      return false;
    }

    // [ISSUE] handle returning multiple array type
    // (for now just assume always a single symbol)

    // make sure to return a non-writable...
    // [ISSUE] cleanup SymbolPool to avoid mistakes (part of ReadablePtr/WritablePtr cleanup?)
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
