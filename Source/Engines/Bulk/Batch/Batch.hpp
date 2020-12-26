#pragma once

#include "../Utils/UniqueTypeId.hpp"

#include "../../../Expression.hpp"

namespace boss::engines::bulk {

class BatchFactory;
class Batch {
public:
  virtual ~Batch() = default;

  virtual Batch* clone() = 0;

  virtual size_t size() const = 0;

  virtual UniqueId::type typeId() const = 0;
  virtual UniqueId::type evaluatedTypeId() const = 0;
  virtual UniqueId::type elementTypeId() const = 0;

  virtual bool isRLE() const = 0;
  virtual bool canContain(Expression::ArgumentType const& val) const = 0;

  virtual void insert(Expression::ArgumentType const& val) = 0;

  virtual Batch* evaluate(BatchFactory const&) = 0;
};

} // namespace boss::engines::bulk
