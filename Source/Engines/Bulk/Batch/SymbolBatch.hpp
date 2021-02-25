#pragma once

#include "RLEBatch.hpp"

#include "../BatchFactory.hpp"
#include "../BatchHelper.hpp"
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

  explicit SymbolBatch(BatchFactory const& factory, size_t count = 0)
      : m_factory(factory), RLEBatch(count, Symbol("")) {}
  explicit SymbolBatch(BatchFactory const& factory, Symbol const& symbol)
      : m_factory(factory), RLEBatch(symbol) {}
  explicit SymbolBatch(BatchFactory const& factory, Symbol&& symbol)
      : m_factory(factory), RLEBatch(std::move(symbol)) {}
  SymbolBatch(BatchFactory const& factory, size_t size, Symbol const& symbol)
      : m_factory(factory), RLEBatch(size, symbol) {}
  SymbolBatch(BatchFactory const& factory, size_t size, Symbol&& symbol)
      : m_factory(factory), RLEBatch(size, std::move(symbol)) {}

  SymbolBatch(SymbolBatch const& other, bool clear = false)
      : m_factory(other.m_factory), RLEBatch(other, clear) {}
  SymbolBatch(SymbolBatch&& other, bool clear = false) noexcept
      : m_factory(other.m_factory), RLEBatch(std::move(other), clear) {}

  ~SymbolBatch() override = default;
  SymbolBatch& operator=(SymbolBatch const& other) = delete;
  SymbolBatch& operator=(SymbolBatch&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsSymbolBatch(clear));
  }
  WritableBatchPtr<RLEBatch> cloneAsRLEBatch(bool clear = false) const override {
    return WritableBatchPtr<RLEBatch>(cloneAsSymbolBatch(clear));
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

    // make sure to return a non-writable...
    // TODO: cleanup SymbolPool to avoid mistakes
    auto const& batchPtr = DefaultSymbolPool::instance().findSymbol(*begin());
    if(!batchPtr) {
      return false;
    }

    outputPtr = batchPtr;

    /*if(size() > 1) {
      auto const& outputBatch = *outputPtr;
      if(outputBatch.elementTypeId() == UniqueId::forType<ComplexExpression>()) {
        // make shallow copies of the batch in an outer compound batch
        // TODO: some type of batches (not the tables!) can be extended like a RLE batch
        auto const& key = m_factory.toKey(outputBatch);
        WritableBatchPtr<CompoundBatch> outerBatchPtr(new CompoundBatch(m_factory));
        auto &outerBatch = *outerBatchPtr;
        for(size_t i = 0; i < size(); ++i) {
          outerBatch.insert(key, i, outputPtr);
        }
        outputPtr = std::move(outerBatchPtr);
      } else {
        // just extend the size of the batch
        // TODO: do we need to handle the case where the batch itself has size != 1?
        auto writableOutputPtr = outputBatch.clone(true);
        auto const& value = m_factory.revertToExpression(std::move(outputPtr));
        auto& writableOutputBatch = *writableOutputPtr;
        writableOutputBatch.resize(size(), value);
        outputPtr = std::move(writableOutputPtr);
      }
    }*/

    return true;
  }

private:
  BatchFactory const& m_factory;
};

} // namespace boss::engines::bulk
