#pragma once

#include "../../../Expression.hpp"
#include "../Executor.hpp"
#include "../ExpressionVisitDispatcher.hpp"
#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class SymbolicFunctions {
  using AnyTypeArgument = typename OperatorUtils::AnyTypeArgument;

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<SymbolOperator>("Symbol");
    operatorRegistry.template registerOperator<FunctionOperatorWithBodyOnly>("Function");
    operatorRegistry.template registerOperator<FunctionOperatorWithParametersAndBody>("Function");
    operatorRegistry.template registerOperator<FunctionOperator>("Function");
    operatorRegistry.template registerOperator<UnevaluatedOperator>("Unevaluated");
  }

private:
  class SymbolOperator : public Operator<AllowedArguments<std::string>> {
  public:
    template <typename ValueType> BulkExpression evaluate(ValueType const& value) const {
      return evaluateSymbol(Symbol(value));
    }

  private:
    BulkExpression evaluateSymbol(Symbol const& symbol) const {
      auto const& symbolPtr = DefaultSymbolRegistry::instance().findSymbol(symbol);
      if(symbolPtr) {
        return *symbolPtr;
      }

      // alternatively, check if this is the head of an operator.
      // if so, build it as a function with arguments
      auto numParameters = OperatorRegistry::instance().findOperatorAndGetNumParameters(symbol);
      if(numParameters >= 0) {
        static BulkExpressionArguments parameterArgs;
        for(size_t i = parameterArgs.size(); i < numParameters; ++i) {
          parameterArgs.emplace_back(Symbol("$arg" + std::to_string(i)));
        }
        BulkComplexExpression parameters(Symbol("List"), parameterArgs);
        BulkComplexExpression body(symbol, parameterArgs);
        return BulkComplexExpression(Symbol("Function"), {parameters, body});
      }
      return symbol;
    }
  };

  class FunctionOperatorWithBodyOnly : public OperatorHoldAllArguments<AnyTypeArgument> {
  public:
    template <typename BodyType> BulkExpression evaluate(BodyType const& body) const {
      // add an empty parameter list, but we don't evaluate yet (arguments aren't provided yet)
      auto parameters = BulkComplexExpression(Symbol("List"), {});
      return BulkComplexExpression(Symbol("Function"), {parameters, body});
    }
  };

  class FunctionOperatorWithParametersAndBody
      : public OperatorHoldAllArguments<AllowedArguments<Symbol, BulkComplexExpression>,
                                        AnyTypeArgument> {
  public:
    template <typename ParameterType, typename BodyType>
    BulkExpression evaluate(ParameterType const& parameters, BodyType const& body) const {
      // we don't evaluate yet (arguments aren't provided yet)
      return BulkComplexExpression(Symbol("Function"), {parameters, body});
    }
  };

  class FunctionOperator
      : public OperatorHoldAllArguments<AllowedArguments<Symbol, BulkComplexExpression>,
                                        AnyTypeArgument, AllowedArguments<BulkComplexExpression>> {
  public:
    template <typename ParameterType, typename BodyType>
    BulkExpression evaluate(ParameterType const& parameters, BodyType const& body,
                            BulkComplexExpression const& arguments) const {
      // make sure the parameter list is evaluated
      // since the Function call itself wouldn't evaluate it
      auto evaluatedParameters = Executor::evaluate(parameters);

      BulkExpression output;
      bool evaluated = ExpressionVisitDispatcher<Symbol, BulkComplexExpression>::visit(
          [this, &output, &body, &arguments](auto const& typedEvaluatedParameters) {
            output = evaluateFunction(typedEvaluatedParameters, body, arguments);
          },
          evaluatedParameters);

      if(evaluated) {
        return output;
      }
      return BulkComplexExpression(Symbol("Function"), {parameters, body, arguments});
    }

  private:
    template <typename ParameterType, typename BodyType>
    BulkExpression evaluateFunction(ParameterType const& parameters, BodyType const& body,
                                    BulkComplexExpression const& arguments) const {
      // store existing symbols
      // to retrieve later in case of name collision
      // (and make sure they are not destroyed while dereferenced...)
      auto const& argList = arguments.getArguments();
      using StoredSymbolPtr = DefaultSymbolRegistry::StoredTypePtr;
      std::vector<std::pair<StoredSymbolPtr&, StoredSymbolPtr>> oldSymbols;
      oldSymbols.reserve(argList.size());
      auto registerArgument = [&oldSymbols](Symbol const& parameter, auto const& value) {
        auto& symbolPtr = DefaultSymbolRegistry::instance().findSymbol(parameter);
        oldSymbols.emplace_back(symbolPtr, std::move(symbolPtr));

        // set symbol at the function scope
        symbolPtr = std::make_unique<DefaultSymbolRegistry::StoredType>(value);
      };

      // replace parameter symbols with arguments
      // by iterating on both the parameter list and argument list
      auto argIt = argList.begin();
      if constexpr(std::is_same_v<ParameterType, Symbol>) {
        // only a single parameter
        registerArgument(parameters, *argIt);
      } else {
        // parameters is a list of symbols
        auto const& parameterList = parameters.getArguments();
        auto parameterIt = parameterList.begin();
        for(; argIt != argList.end() && parameterIt != parameterList.end();
            ++argIt, ++parameterIt) {
          registerArgument(std::get<Symbol>(*parameterIt), *argIt);
        }
      }

      auto output = Executor::evaluate(body);

      // before finishing, set back any colliding symbol (or clear them)
      for(auto& oldSymbol : oldSymbols) {
        oldSymbol.first = std::move(oldSymbol.second);
      }

      return output;
    }
  };

  class UnevaluatedOperator
      : public OperatorHoldAllArguments<AllowedArguments<Symbol, BulkComplexExpression>> {
  public:
    template <typename ExpressionType>
    BulkExpression evaluate(ExpressionType const& expression) const {
      return expression;
    }
  };
};

} // namespace boss::engines::bulk
