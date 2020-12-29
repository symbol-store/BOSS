#pragma once

#include "Batch.hpp"

#include <vector>

namespace boss::engines::bulk {

template <typename T> class ValueBatch : public Batch {
public:
  using ValueType = T;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<ValueBatch<T>>();

  ValueBatch(ValueBatch const&) = default;
  ValueBatch(size_t size = 0) : m_values(size) {}
  ValueBatch(size_t size, T const& value) : m_values(size, value) {}
  ValueBatch(size_t size, T&& value) : m_values(size, std::move(value)) {}
  ValueBatch(std::vector<T> const& values) : m_values(values) {}
  ValueBatch(std::vector<T>&& values) : m_values(std::move(values)) {}

  Batch* clone() override { return new ValueBatch(*this); }

  auto begin() const { return m_values.begin(); }
  auto end() const { return m_values.end(); }

  auto begin() { return m_values.begin(); }
  auto end() { return m_values.end(); }

  size_t size() const override { return m_values.size(); }

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type evaluatedTypeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<T>(); }

  using RLE = std::bool_constant<false>;
  bool isRLE() const override { return RLE::value; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<T>(val);
  }

  void insert(Expression const& val) override {
    m_values.push_back(std::get<T>(val));
  }

  Batch* evaluate(BatchFactory const&) override { return this->clone(); }

private:
  std::vector<T> m_values;
};

} // namespace boss::engines::bulk
