#pragma once

#include "Batch.hpp"

#include "../BatchHelper.hpp"
#include "../Dispatcher.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace boss::engines::bulk {

class CompoundBatch : public Batch {
public:
  using ValueType = ComplexExpression;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<CompoundBatch>();

  UniqueId::type baseId() const override { return UniqueId; }
  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool isRLE() const override { return false; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  using BatchList = std::vector<std::unique_ptr<Batch>>;

  explicit CompoundBatch(BatchFactory const& factory, bool decomposedDispatch = false,
                         Symbol const& symbol = Symbol("List"))
      : m_dispatcher(factory, decomposedDispatch), m_symbol(symbol) {}

  CompoundBatch(CompoundBatch const& other, bool clear = false)
      : m_dispatcher(other.m_dispatcher, clear), m_symbol(other.m_symbol) {}

  void setDecomposedDispatch(bool value) { m_dispatcher.setDecomposed(value); }

  Symbol const& getHead() const { return m_symbol; }

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(new CompoundBatch(*this, clear));
  }
  void clear() override { m_dispatcher.clear(); }

  void reserve(size_t size) override {
    // TODO
  }

  void resize(size_t size, Expression const& val) override { m_dispatcher.resize(size, val); }

  size_t size() const override { return m_dispatcher.size(); }

  template <typename DispatcherIterator> class ConstIterator {
  public:
    explicit ConstIterator(DispatcherIterator dispatcherIt) : m_dispatcherIt(dispatcherIt) {}
    BatchPtr const& operator*() { return m_dispatcherIt->second; }
    bool operator!=(ConstIterator& rhs) { return m_dispatcherIt != rhs.m_dispatcherIt; }
    bool operator!=(ConstIterator&& rhs) { return m_dispatcherIt != rhs.m_dispatcherIt; }
    ConstIterator operator+(size_t incr) const {
      return ConstIterator(std::next(m_dispatcherIt, incr));
    }
    void operator++() { m_dispatcherIt++; }

  protected:
    DispatcherIterator m_dispatcherIt;
  };

  template <typename DispatcherIterator> class Iterator : public ConstIterator<DispatcherIterator> {
  public:
    explicit Iterator(DispatcherIterator dispatcherIt)
        : ConstIterator<DispatcherIterator>(dispatcherIt) {}
    BatchPtr& operator*() { return this->m_dispatcherIt->second; }
    Iterator operator+(size_t incr) const {
      return Iterator(std::next(this->m_dispatcherIt, incr));
    }
  };

  auto begin() const { return ConstIterator(m_dispatcher.begin()); }
  auto end() const { return ConstIterator(m_dispatcher.end()); }

  auto begin() { return Iterator(m_dispatcher.begin()); }
  auto end() { return Iterator(m_dispatcher.end()); }

  BatchPtr const& at(size_t index) const { return *(begin() + index); }
  BatchPtr& at(size_t index) { return *(begin() + index); }

  template <typename Func> void visitBatches(Func&& visitor) const {
    return m_dispatcher.visitBatches(std::move(visitor));
  }

  BatchPtr extract(size_t index) const { return m_dispatcher.extract(*this, index); }
  BatchPtr reduce(size_t index) const { return m_dispatcher.reduce(*this, index); }

  void insert(Expression const& expression) override { insert(std::get<ValueType>(expression)); }

  virtual void insert(ValueType const& expression) {
    auto const& argList = expression.getArguments();
    for(size_t argIndex = 0; argIndex < argList.size(); ++argIndex) {
      auto const& arg = argList[argIndex];
      m_dispatcher.insert(arg, argIndex);
    }
  }

  void insert(Expression const& arg, size_t argIndex, BatchPtr batchPtr) {
    m_dispatcher.insert(arg, argIndex, std::move(batchPtr));
  }

  void merge(BatchPtr&& other) override {
    auto& batch = *static_cast<CompoundBatch*>(other.get());
    m_dispatcher.merge(std::move(batch.m_dispatcher));
  }

  BatchPtr evaluate() const override {
    auto newBatchPtr = clone(true);
    auto& newCompoundBatch = *static_cast<CompoundBatch*>(newBatchPtr.get());
    m_dispatcher.visitBatches([&newCompoundBatch](auto const& key, auto const& batch) {
      newCompoundBatch.insert(key.first, key.second, batch.evaluate());
    });
    return newBatchPtr;
  }

protected:
  Dispatcher m_dispatcher;
  Symbol m_symbol;
};

} // namespace boss::engines::bulk
