#pragma once
#include "Expression.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"

namespace interpreter {

class Interpreter {
public:
  explicit Interpreter(new_runtime::Database* database): database(database) {}

  boss::Expression evaluate(boss::Expression e);

private:
  new_runtime::Database* database;

  std::vector<boss::Expression> evaluateArguments(boss::ComplexExpression const& expression);
};

}