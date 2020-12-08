#pragma once

#include "Expression.hpp"
namespace boss {
class Engine {

public:
  Expression::ReturnType evaluate(Expression::ArgumentType const& arg);
  Expression::ReturnType evaluate(Expression const& e);
};

} // namespace boss
