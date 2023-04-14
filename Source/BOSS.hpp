#pragma once
#include "BOSS.h"
#include "Engine.hpp"
#include "Expression.hpp"

struct BOSSExpression {
  boss::Expression delegate;
};
struct BOSSSymbol {
  boss::Symbol delegate;
};

namespace boss {
expressions::Expression evaluate(expressions::Expression&& expr);
}
