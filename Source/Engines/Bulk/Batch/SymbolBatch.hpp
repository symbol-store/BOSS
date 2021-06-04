#pragma once

#include "ValueBatch.hpp"

#include "../SymbolRegistry.hpp"

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

  explicit SymbolBatch(size_t count = 0) : ValueBatch(count) {}
  explicit SymbolBatch(Symbol const& symbol) : ValueBatch(0, symbol) {}
  explicit SymbolBatch(Symbol&& symbol) : ValueBatch(0, std::move(symbol)) {}
  SymbolBatch(size_t size, Symbol const& symbol) : ValueBatch(size, symbol) {}
  SymbolBatch(size_t size, Symbol&& symbol) : ValueBatch(size, std::move(symbol)) {}

  SymbolBatch(arrow::ArrayVector&& arrays, std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder)
      : ValueBatch(std::move(arrays), std::move(arrayBuilder)) {}

  SymbolBatch(SymbolBatch const& other, bool clear = false) : ValueBatch(other, clear) {}
  SymbolBatch(SymbolBatch&& other, bool clear = false) noexcept
      : ValueBatch(std::move(other), clear) {}

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

    // TODO: handle returning multiple array type
    // (for now just assume always a single symbol)

    // make sure to return a non-writable...
    // [https://github.com/symbol-store/BOSS/issues/90] cleanup SymbolRegistry to avoid mistakes
    auto const& batchPtr = DefaultSymbolRegistry::instance().findSymbol(*begin());
    if(!batchPtr) {
      return false;
    }

    //outputPtr = batchPtr;

    return true;
  }
};

} // namespace boss::engines::bulk
