#pragma once

#include "Batch.hpp"

namespace boss::engines::bulk {

template <typename T> class RLEBatch : public Batch {
public:
  using ValueType = T;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<RLEBatch<T>>();

  RLEBatch(RLEBatch const&) = default;
  RLEBatch() : m_value(T()), m_count(0) {}
  RLEBatch(size_t size, T const& value) : m_value(value), m_count(size) {}
  RLEBatch(size_t size, T&& value) : m_value(std::move(value)), m_count(size) {}
  RLEBatch(T const& value) : m_value(value), m_count(0) {}
  RLEBatch(T&& value) : m_value(std::move(value)), m_count(0) {}

  Batch* clone() override { return new RLEBatch(*this); }

  class ConstIterator {
  public:
    ConstIterator(T const* pointer) : m_pointer(pointer) {}
    T const& operator*() { return *m_pointer; }
    bool operator!=(ConstIterator& rhs) { return m_pointer != rhs.m_pointer; }
    bool operator!=(ConstIterator&& rhs) { return m_pointer != rhs.m_pointer; }
    void operator++() {}

  private:
    T const* m_pointer;
  };

  // readable-only RLE: loop indefinitely on the same element
  ConstIterator begin() const { return ConstIterator(&m_value); }
  ConstIterator end() const { return ConstIterator(nullptr); }

  // writable RLE: stop after first iteration
  T* begin() { return &m_value; }
  T* end() { return &m_value + 1; }

  size_t size() const override { return m_count; }

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type evaluatedTypeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<T>(); }

  using RLE = std::bool_constant<true>;
  bool isRLE() const override { return RLE::value; }
  
  bool canContain(Expression::ArgumentType const& val) const override {
    if constexpr(std::is_constructible_v<Expression::ArgumentType, T>) {
      if(auto* value = std::get_if<T>(&val)) {
        if constexpr(std::is_same_v<T, Expression::Symbol>) {
          if(value->getName() == m_value.getName()) {
            return true;
          }
        } else {
          if(*value == m_value) {
            return true;
          }
        }
      }
    }
    return false;
  }

  void insert(Expression::ArgumentType const& val) override {
    ++m_count;
  }

  Batch* evaluate(BatchFactory const&) override { return this->clone(); }

protected:
  T m_value;
  size_t m_count;
};

} // namespace boss::engines::bulk
