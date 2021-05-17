#include "Interpreter.hpp"
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

boss::Expression Interpreter::evaluate(boss::Expression e) {
  Expression returnValue;

  std::map<std::string, std::function<Expression(ComplexExpression)>> symbolMap{
      {"CollectTuples",
       [&](ComplexExpression e) {
         auto newArgs = evaluateArguments(e);
         boss::engines::mlir::Engine engine(*database);
         return engine.evaluate(ComplexExpression(e.getHead(), newArgs));
       }},
      {"Join", [&](ComplexExpression e) {
         auto newArgs = evaluateArguments(e);
         auto rightTupleStream = newArgs[2];
         auto hashTableBuild = "BuildHashTable"_(newArgs[0], rightTupleStream);

         boss::engines::mlir::Engine engine(*database);
         auto hashTable = engine.evaluate(hashTableBuild);

         auto tablePtr = reinterpret_cast<runtime::hash::HashTable*>(std::get<size_t>(hashTable));

         std::cout << tablePtr->getRelation()->ToString() << std::endl;

         std::cout << hashTable << std::endl;

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
