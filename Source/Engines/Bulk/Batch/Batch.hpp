#pragma once

#include "../Utils/UniqueTypeId.hpp"

#include "../../../Expression.hpp"

#include <memory>

namespace boss::engines::bulk {

class Batch;
using BatchPtr = std::unique_ptr<Batch>;

class Batch {
public:
  virtual ~Batch() = default;

  virtual BatchPtr clone(bool clear = false) const = 0;
  virtual void clear() = 0;

  virtual size_t size() const = 0;

  virtual void reserve(size_t size) = 0;
  virtual void resize(size_t size, Expression const& val) = 0;

  virtual UniqueId::type typeId() const = 0;
  virtual UniqueId::type baseId() const = 0;
  virtual UniqueId::type elementTypeId() const = 0;

  virtual bool isRLE() const = 0;
  virtual bool canContain(Expression const& val) const = 0;

  virtual void insert(Expression const& val) = 0;
  virtual void merge(BatchPtr&& other) = 0;

  virtual BatchPtr evaluate() const = 0;
};

} // namespace boss::engines::bulk
