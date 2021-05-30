#include "ValueConversion.hpp"
#include <map>

namespace boss::mlir::conversion {
using namespace boss::mlir::types;

// Retrieve boss::Expression from Symbol/Argument based on given type
const std::map<RuntimeTypes, std::function<boss::Expression(SymbolArgumentValue)>> typeToExpression {
    {RuntimeTypes::INT, [](SymbolArgumentValue value) { return value.integerValue; }},
    {RuntimeTypes::BOOLEAN, [](SymbolArgumentValue value) { return value.booleanValue; }},
    {RuntimeTypes::FLOAT, [](SymbolArgumentValue value) { return value.floatValue; }},
    {RuntimeTypes::SYMBOL,
        [](SymbolArgumentValue value) { return mExpressionFromSExpression(value.symbolValue); }},
    {RuntimeTypes::STRING,
        [](SymbolArgumentValue value) {
       auto runtimeString = value.stringValue;
       return std::string(runtimeString->data, 0, runtimeString->length);
     }}};

boss::Expression mExpressionFromSExpression(SymbolExpression* expr) {
  if(expr->arguments == nullptr) {
    return boss::Symbol{expr->head};
  }

  auto argc = expr->argc;

  boss::ExpressionArguments args;

  for(int i = 0; i < argc; i++) {
    auto const& argument = expr->arguments[i];

    args.push_back(typeToExpression.at(argument.type)(argument.value));
  }

  return boss::ComplexExpression{boss::Symbol{expr->head}, args};
}
}
