#pragma once
#include "Expression.hpp"
namespace boss {
class Engine {

public:
  virtual Expression evaluate(Expression const& e) = 0;
};

} // namespace boss
