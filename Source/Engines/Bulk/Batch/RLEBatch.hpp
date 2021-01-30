#pragma once

#include "Batch.hpp"

namespace boss::engines::bulk {

template <typename T> class RLEBatch : public Batch {
public:
  using ValueType = T;
  using IsRLE = std::bool_constant<true>;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<RLEBatch<T>>();

  UniqueId::type baseId() const override { return UniqueId; }
  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool isRLE() const override { return IsRLE::value; }

  bool canContain(Expression const& val) const override {
    if(m_count == 0) {
      return true;
    }

    if(auto* value = std::get_if<ValueType>(&val)) {
      if(*value == m_value) {
        return true;
      }
    }

    return false;
  }

  RLEBatch(size_t size, ValueType const& value) : m_value(value), m_count(size) {}
  RLEBatch(size_t size, ValueType&& value) : m_value(std::move(value)), m_count(size) {}
  explicit RLEBatch(ValueType const& value) : m_value(value), m_count(0) {}
  explicit RLEBatch(ValueType&& value) : m_value(std::move(value)), m_count(0) {}

  RLEBatch(RLEBatch const& other) : m_value(other.m_value), m_count(other.m_count) {}
  RLEBatch(RLEBatch&& other) noexcept
      : m_value(std::move(other.m_value)), m_count(std::move(other.m_count)) {}

  ~RLEBatch() override = default;
  RLEBatch& operator=(RLEBatch const& other) = delete;
  RLEBatch& operator=(RLEBatch&& other) = delete;

  BatchPtr clone(bool clear = false) const override { return cloneAsRLEBatch(clear); }

  using RLEBatchPtr = std::unique_ptr<RLEBatch>;
  virtual RLEBatchPtr cloneAsRLEBatch(bool clear = false) const {
    return RLEBatchPtr(clear ? new RLEBatch(m_value) : new RLEBatch(*this));
  }

  void clear() override { m_count = 0; }

  // readable-only RLE: safe to loop indefinitely on the same element
  // but will stop if we compare with end()
  class ConstIterator {
  public:
    explicit ConstIterator(ValueType const& pointer, size_t size)
        : m_pointer(pointer), m_left(size) {}
    ValueType const& operator*() { return m_pointer; }
    bool operator!=(ConstIterator& rhs) { return m_left != rhs.m_left; }
    bool operator!=(ConstIterator&& rhs) { return m_left != rhs.m_left; }
    ConstIterator operator+(size_t incr) const {
      return ConstIterator(m_pointer, m_left > incr ? m_left - incr : 0);
    }
    void operator++() {
      if(m_left > 0) {
        m_left--;
      }
    }

  private:
    ValueType const& m_pointer;
    size_t m_left;
  };
  auto begin() const { return ConstIterator(m_value, m_count); }
  auto end() const { return ConstIterator(m_value, 0); }

  // writable RLE: stop after first iteration
  class Iterator {
  public:
    explicit Iterator(ValueType* pointer) : m_pointer(pointer) {}
    ValueType& operator*() { return *m_pointer; }
    bool operator!=(Iterator& rhs) { return m_pointer != rhs.m_pointer; }
    bool operator!=(Iterator&& rhs) { return m_pointer != rhs.m_pointer; }
    Iterator operator+(size_t /*size*/) const { return Iterator(nullptr); }
    void operator++() { m_pointer = nullptr; }

  private:
    ValueType* m_pointer;
  };
  auto begin() { return Iterator(m_count > 0 ? &m_value : nullptr); }
  auto end() { return Iterator(nullptr); }

  void reserve(size_t size) override {
    // nothing to do
  }
  void resize(size_t size, Expression const& val) override {
    if(m_count == 0) {
      insert(val);
    }
    m_count = size;
  }

  size_t size() const override { return m_count; }

  void insert(Expression const& expression) override { insert(std::get<ValueType>(expression)); }

  void insert(ValueType const& value) {
    m_value = value;
    ++m_count;
  }

  void merge(BatchPtr&& other) override {
    auto& batch = *static_cast<RLEBatch*>(other.get());
    m_value = batch.m_value;
    m_count += batch.m_count;
  }

  BatchPtr evaluate() const override { return this->clone(); }

private:
  ValueType m_value;
  size_t m_count;
};

} // namespace boss::engines::bulk
