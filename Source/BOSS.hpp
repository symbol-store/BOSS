#pragma once
#include "Engine.hpp"
#include "Expression.hpp"
#include "BOSS.h"

struct BOSSExpression {
  boss::Expression delegate;
};
struct BOSSSymbol {
  boss::Symbol delegate;
};

namespace boss {
expressions::Expression evaluate(expressions::Expression const& expr);
}
