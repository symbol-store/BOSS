#pragma once
#include "Expression.hpp"
namespace boss {
class Engine {

public:
  Expression evaluate(Expression const& e);
  virtual ~Engine() = default;
};

} // namespace boss
