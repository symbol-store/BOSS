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

    if(auto* value = std::get_if<T>(&val)) {
      if(*value == m_value) {
        return true;
      }
    }

    return false;
  }

  RLEBatch(RLEBatch const&) = default;
  RLEBatch(size_t size, T const& value) : m_value(value), m_count(size) {}
  RLEBatch(size_t size, T&& value) : m_value(std::move(value)), m_count(size) {}
  RLEBatch(T const& value) : m_value(value), m_count(0) {}
  RLEBatch(T&& value) : m_value(std::move(value)), m_count(0) {}

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(clear ? new RLEBatch(m_value) : new RLEBatch(*this));
  }

  void clear() override { m_count = 0; }

  class ConstIterator {
  public:
    ConstIterator(T const* pointer) : m_pointer(pointer) {}
    T const& operator*() { return *m_pointer; }
    bool operator!=(ConstIterator& rhs) { return m_pointer != rhs.m_pointer; }
    bool operator!=(ConstIterator&& rhs) { return m_pointer != rhs.m_pointer; }
    ConstIterator operator+(size_t) const { return *this; }
    void operator++() {}

  private:
    T const* m_pointer;
  };

  // readable-only RLE: loop indefinitely on the same element
  ConstIterator begin() const { return ConstIterator(m_count > 0 ? &m_value : nullptr); }
  ConstIterator end() const { return ConstIterator(nullptr); }

  // writable RLE: stop after first iteration
  T* begin() { return m_count > 0 ? &m_value : nullptr; }
  T* end() { return m_count > 0 ? &m_value + 1 : nullptr; }

  size_t size() const override { return m_count; }

  void insert(Expression const& val) override {
    if(m_count == 0) {
      m_value = std::get<T>(val);
    }
    ++m_count;
  }

  BatchPtr evaluate() const override { return this->clone(); }

protected:
  T m_value;
  size_t m_count;
};

} // namespace boss::engines::bulk
