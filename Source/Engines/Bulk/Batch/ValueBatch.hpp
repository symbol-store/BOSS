#pragma once

#include "Batch.hpp"

#include <vector>

namespace boss::engines::bulk {

template <typename T> class ValueBatch : public Batch {
public:
  using ValueType = T;
  using IsRLE = std::bool_constant<false>;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<ValueBatch<T>>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool isRLE() const override { return IsRLE::value; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  ValueBatch(ValueBatch const&) = default;
  ValueBatch(size_t size = 0) : m_values(size) {}
  ValueBatch(size_t size, T const& value) : m_values(size, value) {}
  ValueBatch(size_t size, T&& value) : m_values(size, std::move(value)) {}
  ValueBatch(std::vector<T> const& values) : m_values(values) {}
  ValueBatch(std::vector<T>&& values) : m_values(std::move(values)) {}

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(clear ? new ValueBatch() : new ValueBatch(*this));
  }

  void clear() override { m_values.clear(); }

  auto begin() const { return m_values.begin(); }
  auto end() const { return m_values.end(); }

  auto begin() { return m_values.begin(); }
  auto end() { return m_values.end(); }

  size_t size() const override { return m_values.size(); }

  void insert(Expression const& val) override { m_values.push_back(std::get<T>(val)); }

  BatchPtr evaluate() const override { return this->clone(); }

private:
  std::vector<T> m_values;
};

} // namespace boss::engines::bulk
