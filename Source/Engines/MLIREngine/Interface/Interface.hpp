#pragma once

#include "Engines/MLIREngine/AST/Expression.hpp"
#include <memory>

struct Interface {

  template <typename Expr> std::unique_ptr<Expr> evaluate(mlirengine::Expression& e);

private:
  int64_t internalEvaluate(mlirengine::Expression& e);
};
