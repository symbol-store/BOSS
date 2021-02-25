#pragma once

#include "Batch.hpp"

namespace boss::engines::bulk {

template <typename T> class RLEBatch : public Batch {
public:
  using ValueType = T;
  using IsRLE = std::bool_constant<true>;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<RLEBatch<T>>();

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

  RLEBatch(RLEBatch const& other, bool clear = false)
      : m_value(other.m_value), m_count(clear ? 0 : other.m_count) {}
  RLEBatch(RLEBatch&& other, bool clear = false) noexcept
      : m_value(std::move(other.m_value)), m_count(clear ? 0 : std::move(other.m_count)) {}

  ~RLEBatch() override = default;
  RLEBatch& operator=(RLEBatch const& other) = delete;
  RLEBatch& operator=(RLEBatch&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsRLEBatch(clear));
  }
  virtual WritableBatchPtr<RLEBatch> cloneAsRLEBatch(bool clear = false) const {
    return WritableBatchPtr(new RLEBatch(*this, clear));
  }

  template <typename BatchType, std::enable_if_t<std::is_base_of_v<BatchType, RLEBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsRLEBatch(clear);
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

  void merge(ReadablePtr&& other) override {
    auto const& batch = *static_cast<RLEBatch const*>(other.get());
    m_value = batch.m_value;
    m_count += batch.m_count;
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    outputPtr.reset();
    return false;
  }

private:
  ValueType m_value;
  size_t m_count;
};

} // namespace boss::engines::bulk
