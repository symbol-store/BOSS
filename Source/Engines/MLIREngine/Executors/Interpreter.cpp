#include "Interpreter.hpp"
#include "Compiler.hpp"
#include "Engines/MLIREngine.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include "Engines/MLIREngine/Runtime/HashTable.hpp"
#include <iostream>

using boss::utilities::operator""_;
using namespace boss;
using namespace interpreter;

std::vector<Expression> Interpreter::evaluateArguments(ComplexExpression const& expression) {
  std::vector<Expression> newArgs;
  std::transform(expression.getArguments().begin(), expression.getArguments().end(),
                 std::back_inserter(newArgs), [&](auto e) { return evaluate(e); });
  return newArgs;
}

template<typename T>
Expression Interpreter::evaluateArithmeticOperator(ComplexExpression e, std::function<T(T, T)> Op, T initialVal) {
  // TODO make it work for non-abelian groups
  auto evaluatedArgs = evaluateArguments(e);

  auto sum = initialVal;
  std::vector<Expression> newArgs;
  for (auto const& arg : evaluatedArgs) {
    if (std::holds_alternative<T>(arg)) {
      sum = Op(sum, std::get<T>(arg));
    } else {
      newArgs.emplace_back(arg);
    }
  }

  if (newArgs.size() == 0) {
    return sum;
  } else {
    newArgs.push_back(sum);
    return ComplexExpression(e.getHead(), newArgs);
  }
}

boss::Expression Interpreter::evaluate(boss::Expression e) {
  Expression returnValue;

  std::map<std::string, std::function<Expression(ComplexExpression)>> symbolMap{
      {"Plus", [&](ComplexExpression e) -> Expression {
        return evaluateArithmeticOperator<int>(e, [](int a, int b) { return a + b; }, 0);
       }},

      {"Mul", [&](ComplexExpression e) -> Expression {
        return evaluateArithmeticOperator<int>(e, [](int a, int b) { return a - b; }, 0);
      }},

      {"StringJoin", [&](ComplexExpression e) -> Expression {
         return evaluateArithmeticOperator<std::string>(e, [](auto a, auto b) { return a + b;}, "");
       }},

      {"Greater", [&](ComplexExpression e) -> Expression {
         auto newArgs = evaluateArguments(e);

         if (!std::holds_alternative<int>(newArgs[0]) || !std::holds_alternative<int>(newArgs[1])) {
           return ComplexExpression(e.getHead(), newArgs);
         }

         return std::get<int>(newArgs[0]) > std::get<int>(newArgs[1]);
       }},

      {"Symbol", [&](ComplexExpression e) -> Expression {
         auto newArgs = evaluateArguments(e);

         if (!std::holds_alternative<std::string>(newArgs[0])) {
           return ComplexExpression(e.getHead(), newArgs);
         }

         return Symbol(std::get<std::string>(newArgs[0]));
       }},

      // TODO rewriter lambda arguments
      {"CollectTuples",
       [&](ComplexExpression e) {
         auto newArgs = evaluateArguments(e);
         boss::engines::mlir::compiler::Compiler compiler(database);
         return compiler.evaluate(ComplexExpression(e.getHead(), newArgs));
       }},
      {"GroupBy", [&](ComplexExpression e) {
        auto newArgs = evaluateArguments(e);
        boss::engines::mlir::compiler::Compiler compiler(database);
        return compiler.evaluate(ComplexExpression(e.getHead(), newArgs));
       }},
      {"Join", [&](ComplexExpression e) {
         auto newArgs = evaluateArguments(e);
         auto rightTupleStream = newArgs[2];
         auto hashTableBuild = "BuildHashTable"_(newArgs[0], rightTupleStream);

         // Compile and execute the right side of the join
        boss::engines::mlir::compiler::Compiler compiler(database);
         auto hashTable = compiler.evaluate(hashTableBuild);

         return ComplexExpression(e.getHead(),
                                  {newArgs[0], newArgs[1], std::get<size_t>(hashTable)});
       }}};

  std::visit(boss::utilities::overload([&](int a) { returnValue = a; },
                                       [&](char const* a) { returnValue = a; },
                                       [&](Symbol const& a) { returnValue = a; },
                                       [&](std::string const& a) { returnValue = a; },
                                       [&](ComplexExpression const& expression) {
                                         auto const& head = expression.getHead();
                                         auto it = symbolMap.find(head.getName());
                                         if(it != symbolMap.end()) {
                                           returnValue = it->second(expression);
                                         } else {
                                           auto newArguments = evaluateArguments(expression);
                                           returnValue = ComplexExpression(head, newArguments);
                                         }
                                       }),
             e);

  return returnValue;
}
