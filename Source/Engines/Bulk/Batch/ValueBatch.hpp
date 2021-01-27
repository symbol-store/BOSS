#pragma once

#include "Batch.hpp"

#include <vector>

namespace boss::engines::bulk {

template <typename T> class ValueBatch : public Batch {
public:
  using ValueType = T;
  using IsRLE = std::bool_constant<false>;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<ValueBatch<T>>();

  UniqueId::type baseId() const override { return UniqueId; }
  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool isRLE() const override { return IsRLE::value; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  explicit ValueBatch(size_t size = 0) : m_values(size) {}
  explicit ValueBatch(std::vector<ValueType> const& values) : m_values(values) {}
  explicit ValueBatch(std::vector<ValueType>&& values) : m_values(std::move(values)) {}
  ValueBatch(size_t size, ValueType const& value) : m_values(size, value) {}
  ValueBatch(size_t size, ValueType&& value) : m_values(size, std::move(value)) {}
  ValueBatch(ValueBatch const&) = default;

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(clear ? new ValueBatch() : new ValueBatch(*this));
  }

  void clear() override { m_values.clear(); }

  auto begin() const { return m_values.begin(); }
  auto end() const { return m_values.end(); }

  auto begin() { return m_values.begin(); }
  auto end() { return m_values.end(); }

  void reserve(size_t size) override { m_values.reserve(size); }
  void resize(size_t size, Expression const& val) override {
    m_values.resize(size, std::get<ValueType>(val));
  }

  size_t size() const override { return m_values.size(); }

  void insert(Expression const& expression) override { insert(std::get<ValueType>(expression)); }

  void insert(ValueType const& value) { m_values.push_back(value); }

  void merge(BatchPtr&& other) override {
    auto& batch = *static_cast<ValueBatch*>(other.get());
    m_values.insert(m_values.end(), batch.m_values.begin(), batch.m_values.end());
  }

  BatchPtr evaluate() const override { return this->clone(); }

private:
  std::vector<ValueType> m_values;
};

} // namespace boss::engines::bulk
