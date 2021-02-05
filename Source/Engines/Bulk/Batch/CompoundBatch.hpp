#pragma once

#include "Batch.hpp"

#include "../BatchHelper.hpp"
#include "../Dispatcher.hpp"

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

  UniqueId::type baseId() const override { return UniqueId; }
  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool isRLE() const override { return false; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  using BatchList = std::vector<std::unique_ptr<Batch>>;

  explicit CompoundBatch(BatchFactory const& factory, bool decomposedDispatch = false,
                         bool ordered = true)
      : m_dispatcher(factory, decomposedDispatch, ordered),
        m_symbol(ordered ? "List"_ : "MultiSet"_) {}

  explicit CompoundBatch(BatchFactory const& factory, bool decomposedDispatch, bool ordered,
                         Symbol const& symbol)
      : m_dispatcher(factory, decomposedDispatch, ordered), m_symbol(symbol) {}

  CompoundBatch(CompoundBatch const& other, bool clear = false)
      : m_dispatcher(other.m_dispatcher, clear), m_symbol(other.m_symbol) {}

  ~CompoundBatch() override = default;
  CompoundBatch(CompoundBatch&& other) = delete;
  CompoundBatch& operator=(CompoundBatch const& other) = delete;
  CompoundBatch& operator=(CompoundBatch&& other) = delete;

  void setDecomposedDispatch(bool value) { m_dispatcher.setDecomposed(value); }
  void setOrdered(bool value) { m_dispatcher.setOrdered(value); }

  Symbol const& getHead() const { return m_symbol; }

  BatchPtr clone(bool clear = false) const override { return cloneAsCompoundBatch(clear); }

  using CompoundBatchPtr = std::unique_ptr<CompoundBatch>;
  virtual CompoundBatchPtr cloneAsCompoundBatch(bool clear = false) const {
    return CompoundBatchPtr(new CompoundBatch(*this, clear));
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
    BatchPtr& operator*() const { return m_dispatcherIt->second; }
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
    BatchPtr const& operator*() const { return m_dispatcherIt->second; }
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

  BatchPtr const& at(size_t index) const { return *(begin() + index); }
  BatchPtr& at(size_t index) { return *(begin() + index); }

  template <typename Func> void visitBatches(Func&& visitor) const {
    return m_dispatcher.visitBatches(std::forward<Func>(visitor));
  }

  template <typename BatchHelper, typename Func> void visitBatches(Func&& visitor) const {
    return m_dispatcher.visitBatches<BatchHelper>(std::forward<Func>(visitor));
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
    BatchHelper<CompoundBatch>::visit(
        [this](auto&& batch) { m_dispatcher.merge(std::move(batch.m_dispatcher)); }, *other);
  }

  BatchPtr evaluate() const override {
    auto newBatchPtr = cloneAsCompoundBatch(true);
    auto& newCompoundBatch = *newBatchPtr;
    m_dispatcher.visitBatches([&newCompoundBatch](auto const& key, auto const& batch) {
      newCompoundBatch.insert(key.first, key.second, batch.evaluate());
    });
    return newBatchPtr;
  }

  Symbol const& getHead() { return m_symbol; }

private:
  Dispatcher m_dispatcher;
  Symbol m_symbol;
};

} // namespace boss::engines::bulk
