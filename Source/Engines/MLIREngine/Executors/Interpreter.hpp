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
  std::unordered_map<std::string, boss::Expression> symbolTable;

  std::vector<boss::Expression> evaluateArguments(boss::ComplexExpression const& expression);

  template<typename T>
  boss::Expression evaluateArithmeticOperator(boss::ComplexExpression e, std::function<T(T,T)>, T initialVal);
};

}