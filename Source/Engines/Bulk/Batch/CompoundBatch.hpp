#pragma once

#include "Batch.hpp"

#include "../BatchHelper.hpp"
#include "../Dispatcher.hpp"
#include "../SymbolPool.hpp"

#include "../../../Utilities.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace boss::engines::bulk {

using boss::utilities::operator""_;

class CompoundBatch : public Batch {
public:
  using ValueType = ComplexExpression;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<CompoundBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool isRLE() const override { return false; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  explicit CompoundBatch(BatchFactory const& factory, bool decomposed = false, bool ordered = true)
      : m_dispatcher(factory, decomposed, ordered), m_symbol(ordered ? "List"_ : "MultiSet"_) {}

  explicit CompoundBatch(BatchFactory const& factory, Symbol const& symbol, bool decomposed = false)
      : m_dispatcher(factory, decomposed, symbol.getName() != "MultiSet"), m_symbol(symbol) {}

  CompoundBatch(CompoundBatch const& other, bool clear = false)
      : m_dispatcher(other.m_dispatcher, clear), m_symbol(other.m_symbol) {}

  ~CompoundBatch() override = default;
  CompoundBatch(CompoundBatch&& other) = delete;
  CompoundBatch& operator=(CompoundBatch const& other) = delete;
  CompoundBatch& operator=(CompoundBatch&& other) = delete;

  bool isDecomposed() const { return m_dispatcher.isDecomposed(); }
  void setDecomposed(bool value) { m_dispatcher.setDecomposed(value); }

  Symbol const& getHead() const { return m_symbol; }
  void setHead(Symbol const& symbol) { m_symbol = symbol; }
  void setHead(Symbol&& symbol) { m_symbol = std::move(symbol); }

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsCompoundBatch(clear));
  }
  virtual WritableBatchPtr<CompoundBatch> cloneAsCompoundBatch(bool clear = false) const {
    return WritableBatchPtr(new CompoundBatch(*this, clear));
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, CompoundBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsCompoundBatch(clear);
  }

  void clear() override { m_dispatcher.clear(); }

  void reserve(size_t size) override {
    // TODO
  }

  void resize(size_t size, Expression const& val) override { m_dispatcher.resize(size, val); }

  size_t size() const override { return m_dispatcher.size(); }

  template <typename DispatcherIterator> class Iterator {
  public:
    explicit Iterator(DispatcherIterator dispatcherIt) : m_dispatcherIt(dispatcherIt) {}
    ReadablePtr& operator*() const { return m_dispatcherIt->second; }
    bool operator!=(Iterator const& rhs) const { return m_dispatcherIt != rhs.m_dispatcherIt; }
    bool operator!=(Iterator&& rhs) const { return m_dispatcherIt != rhs.m_dispatcherIt; }
    Iterator operator+(size_t incr) const { return Iterator(std::next(m_dispatcherIt, incr)); }
    void operator++() { m_dispatcherIt++; }

  private:
    DispatcherIterator m_dispatcherIt;
  };

  template <typename DispatcherIterator> class ConstIterator {
  public:
    explicit ConstIterator(DispatcherIterator dispatcherIt) : m_dispatcherIt(dispatcherIt) {}
    ReadablePtr const& operator*() const { return m_dispatcherIt->second; }
    bool operator!=(ConstIterator const& rhs) const { return m_dispatcherIt != rhs.m_dispatcherIt; }
    bool operator!=(ConstIterator&& rhs) const { return m_dispatcherIt != rhs.m_dispatcherIt; }
    ConstIterator operator+(size_t incr) const {
      return ConstIterator(std::next(m_dispatcherIt, incr));
    }
    void operator++() { m_dispatcherIt++; }

  private:
    DispatcherIterator m_dispatcherIt;
  };

  auto begin() const { return ConstIterator(m_dispatcher.begin()); }
  auto end() const { return ConstIterator(m_dispatcher.end()); }

  auto begin() { return Iterator(m_dispatcher.begin()); }
  auto end() { return Iterator(m_dispatcher.end()); }

  template <typename Func> void visitBatches(Func&& visitor) const {
    return m_dispatcher.visitBatches(std::forward<Func>(visitor));
  }

  template <typename BatchHelper, typename Func> void visitBatches(Func&& visitor) const {
    return m_dispatcher.visitBatches<BatchHelper>(std::forward<Func>(visitor));
  }

  virtual ReadablePtr extract(size_t index) const { return m_dispatcher.extract(*this, index); }
  virtual ReadablePtr reduce(size_t index) const { return m_dispatcher.reduce(*this, index); }

  void insert(Expression const& expression) override { insert(std::get<ValueType>(expression)); }

  virtual void insert(ValueType const& expression) {
    auto const& argList = expression.getArguments();
    for(size_t argIndex = 0; argIndex < argList.size(); ++argIndex) {
      auto const& arg = argList[argIndex];
      m_dispatcher.insert(arg, argIndex);
    }
  }

  void insert(Expression const& arg, size_t argIndex, ReadablePtr batchPtr) {
    m_dispatcher.insert(arg, argIndex, std::move(batchPtr));
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    // set the local tuple to be accessible by the row values
    auto& symbolPtr = DefaultSymbolPool::instance().findSymbol(Symbol("$tuple"));
    auto backupSymbol = std::move(symbolPtr);
    symbolPtr = Batch::ReadablePtr(shared_from_this());

    bool anyEvaluated = false;
    auto newCompoundPtr = cloneAsCompoundBatch(true);
    auto& newCompoundBatch = *newCompoundPtr;
    m_dispatcher.visitEvaluated(
        [&newCompoundBatch, &anyEvaluated](auto const& key, auto batchPtr, bool evaluated) {
          newCompoundBatch.insert(key.first, key.second, std::move(batchPtr));
          anyEvaluated |= evaluated;
        });

    // reset to any previous local tuple symbol
    symbolPtr = std::move(backupSymbol);

    if(!anyEvaluated) {
      outputPtr.reset();
      return false;
    }

    outputPtr = std::move(newCompoundPtr);
    return true;
  }

  void merge(ReadablePtr&& other) override {
    BatchHelper<CompoundBatch>::visit(
        [this](auto const& batch) { m_dispatcher.merge(batch.m_dispatcher); }, *other);
  }

protected:
  template <typename DerivedBatchType> void merge(ReadablePtr&& other) {
    BatchHelper<DerivedBatchType>::visit(
        [this](auto const& batch) { m_dispatcher.merge(batch.m_dispatcher); }, *other);
  }

private:
  Dispatcher m_dispatcher;
  Symbol m_symbol;
};

} // namespace boss::engines::bulk
