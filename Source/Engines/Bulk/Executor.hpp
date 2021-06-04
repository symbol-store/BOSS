#pragma once

#include "BatchVisitDispatcher.hpp"
#include "ExtendedExpression.hpp"
#include "OperatorRegistry.hpp"

namespace boss::engines::bulk {

/** This is the main evaluate() function to call for a batch.
 * It also contains the logic for:
 * - the evaluation of the arguments (from the leaves to the root)
 * - the resolution of the argument types, making sure it matches the operator's expected types,
 * and passing the list of arguments to the operator's evaluate() function
 */
class Executor {
public:
  /// evaluate without parameters
  static bool evaluate(BulkExpression const& expression, BulkExpression& output) {
    return OperatorRegistry<Executor>::instance().findAndExecuteOperator(expression, output);
  }

  /// evaluate with parameters
  /// function is a symbol or "Function"_(body) or "Function"_(parameters, body)
  template <typename FunctionType>
  static bool evaluate(FunctionType const& function, BulkExpressionArguments const& args,
                       BulkExpression& output) {
    auto processAsHeadOnly = [&args, &output](Symbol const& head) {
      // special case when providing function as just a symbol head
      // construct an expression batch from the head (with single argument)
      Symbol parameter("tuple");
      BulkComplexExpression body(head, {parameter});
      Symbol functionHead("Function");
      return evaluate(BulkComplexExpression(functionHead, {parameter, body}), args, output);
    };

    auto processAsBodyOnly = [&output](BulkExpression const& body) {
      // the predicate has only a body (not dependent on tuple)
      // we can just ignore the parameters and evaluate the body
      if(!evaluate(body, output)) {
        // if it fails to evaluate, return the body itself
        // (e.g. when the body is just an atom)
        output = body;
      }
      return true;
    };

    auto processAsParametersAndBody = [&args, &output](BulkExpression const& parameters,
                                                       BulkExpression const& body) {
      // general case:
      // assume the batch to be composed of a parameter list and a body

      // store existing symbols
      // to retrieve later in case of name collision
      // (and make sure they are not destroyed while dereferenced...)
      using SymbolPtr = DefaultSymbolRegistry::SymbolPtr;
      std::vector<std::pair<SymbolPtr&, SymbolPtr>> oldSymbols;
      oldSymbols.reserve(args.size());
      auto registerArgument = [&oldSymbols](Symbol const& parameter, auto const& value) {
        auto& symbolPtr = DefaultSymbolRegistry::instance().findSymbol(parameter);
        oldSymbols.emplace_back(symbolPtr, std::move(symbolPtr));

        // set symbol at the function scope
        symbolPtr = std::make_unique<DefaultSymbolRegistry::StoredType>(value);
      };

      // replace parameter symbols with arguments
      // by iterating on both the parameter batch and arg list
      auto argIt = args.begin();
      if(std::holds_alternative<Symbol>(parameters)) {
        // only a single parameter
        registerArgument(std::get<Symbol>(parameters), *argIt);
      } else if(std::holds_alternative<BulkComplexExpression>(parameters)) {
        auto parameterList = std::get<BulkComplexExpression>(parameters).getArguments();
        auto parameterIt = parameterList.begin();
        for(; argIt != args.end() && parameterIt != parameterList.end(); ++argIt, ++parameterIt) {
          registerArgument(std::get<Symbol>(*parameterIt), *argIt);
        }
      }

      if(!evaluate(body, output)) {
        // if it fails to evaluate, return the body itself
        // (e.g. when the body is just an atom)
        output = body;
      }

      // before finishing, set back any colliding symbol (or clear them)
      for(auto& oldSymbol : oldSymbols) {
        oldSymbol.first = std::move(oldSymbol.second);
      }
      
      return true;
    };

    if constexpr(std::is_same_v<FunctionType, Symbol>) {
      return processAsHeadOnly(function);
    } else {
      auto const& functionElements = function.getArguments();

      if(functionElements.size() == 0) {
        return processAsHeadOnly(function.getHead());
      }

      if(functionElements.size() == 1) {
        auto const& body = functionElements.front();
        return processAsBodyOnly(body);
      }

      auto functionElementIt = functionElements.begin();
      auto const& parameters = *functionElementIt;
      auto const& body = *(functionElementIt + 1);
      return processAsParametersAndBody(parameters, body);
    }
  }

  /// This is the function called back from the operator registry,
  /// receiving the specific operator type to call evaluate()
  template <typename OperatorType>
  static auto execute(BulkExpression& output, OperatorType const& op,
                      BulkComplexExpression const& expression) {
    return buildArgumentsTupleAndEvaluate(output, op, expression);
  }

private:
  /// calls the evaluation function with specific Batch types as arguments
  /// (not just generic Batch)
  template <typename OperatorType, typename InputBatchTuple, size_t... Indices>
  static BulkExpression evaluateWithTypedArguments(OperatorType const& op, InputBatchTuple&& in,
                                                   std::index_sequence<Indices...> /*unused*/) {
    return op.evaluate(std::get<Indices>(std::forward<InputBatchTuple>(in))...);
  }

  /// build a tuple of specific Batch argument types
  /// from dynamic information extracted from generic Batch list
  template <typename OperatorType, typename... ArgumentBatchTypes>
  static bool buildArgumentsTupleAndEvaluate(
      BulkExpression& output, OperatorType const& op, BulkComplexExpression const& expression,
      size_t batchIndex = 0, std::tuple<ArgumentBatchTypes...>&& argumentsTuple = std::tuple<>()) {
    using ArgumentsTuple = std::tuple<ArgumentBatchTypes...>;
    using OperatorProperties = typename OperatorType::Properties;
    size_t constexpr FuncArgCount = OperatorProperties::ArgumentCount;
    size_t constexpr ArgIndex = sizeof...(ArgumentBatchTypes);
    if constexpr(ArgIndex == FuncArgCount) {
      // We finish to build the argument batches
      // Now, we can pass it to the operator
      if constexpr(std::is_same_v<ArgumentsTuple, std::tuple<>>) {
        output = op.evaluate();
      } else {
        output = evaluateWithTypedArguments(op, std::move(argumentsTuple),
                                            std::make_index_sequence<FuncArgCount>{});
      }

      if constexpr(FuncArgCount == 2) {
        // Special case for binary operators: we can split arguments into pairs
        // to treat a longer argument list as a deeper compound expression.
        // This is needed because the arguments of the evaluation cannot be variadic
        // if we want them to be defined by the Operator at compile time.
        // We can get rid of it once the evaluation is not a lambda function anymore
        if(batchIndex < expression.getArguments().size()) {
          auto firstArg = output;
          bool visited = false;
          bool evaluated = false;
          OperatorProperties::template visitSupportedType<0>(
              [&output, &visited, &evaluated, &op, expression, &batchIndex](auto const& typedArg) {
                visited = true;
                evaluated = buildArgumentsTupleAndEvaluate(output, op, expression, batchIndex,
                                                           std::forward_as_tuple(typedArg));
              },
              firstArg);
          if(visited) {
            return evaluated;
          }
          // Otherwise, the output type wasn't a compatible argument type.
          // In that case, put back the initial output
          // it will just ignore the remaining arguments
          // [https://github.com/symbol-store/BOSS/issues/87] probably related with it
          // better returning full arguments but unevaluated expression
          output = firstArg;
        }
      }
      return true;
    } else {
      // Here is the main part of the function
      // Build the next argument batch...
      auto const& unevaluatedArg = expression.getArguments()[batchIndex];
      auto evaluatedArg = evaluateExpressionArgument<OperatorProperties>(unevaluatedArg, ArgIndex);

      bool visited = false;
      bool evaluated = false;
      // ... get the specific type, add it to the tuple
      // and pass the new tuple to the same function recursively (compile-time recursion)
      OperatorProperties::template visitSupportedType<ArgIndex>(
          [&argumentsTuple, &output, &visited, &evaluated, &op, &expression,
           &batchIndex](auto const& typedArgument) {
            visited = true;
            evaluated = std::apply(
                [&typedArgument, &output, &op, &expression, &batchIndex](auto... arg) {
                  // move evaluated ptr to the derived type
                  return buildArgumentsTupleAndEvaluate(
                      output, op, expression, batchIndex + 1,
                      std::forward_as_tuple((std::move(arg))..., typedArgument));
                },
                std::move(argumentsTuple));
          },
          std::move(evaluatedArg));

      if(visited) {
        // If we reached here, it means that all the remaining args (from the recursion)
        // have been evaluated properly and the operator's evaluation called with them
        // we can just return!
        return evaluated;
      }

      // We reached this portion of the code if an argument type isn't supported by the operator.
      // We need to build an output nevertheless, by evaluating as much as we can the remaining
      // arguments.
      // The argument tuple we built until here hasn't been consumed yet
      // so we can use it for constructing our output.

      BulkExpressionArguments argList;
      argList.reserve(FuncArgCount);

      // add previous evaluated arguments
      std::apply([&argList](auto&&... args) { (..., argList.emplace_back(std::move(args))); },
                 argumentsTuple);

      // add this current batch (at the state we were evaluating it)
      argList.emplace_back(std::move(evaluatedArg));

      // check if they anything has been evaluated with the previous arguments
      bool anyEvaluated = false;
      for(size_t i = 0; i < batchIndex; ++i) {
        auto const& beforeArg = expression.getArguments()[i];
        if(argList[i] != beforeArg) {
          anyEvaluated = true;
          break;
        }
      }

      // still evaluate remaining args as much as possible
      for(size_t i = batchIndex + 1; i < expression.getArguments().size(); ++i) {
        auto argIndex = i < FuncArgCount ? i : FuncArgCount - 1;
        auto const& unevaluatedArg = expression.getArguments()[i];
        auto evaluatedArg =
            evaluateExpressionArgument<OperatorProperties>(unevaluatedArg, argIndex);
        if(evaluatedArg != unevaluatedArg) {
          anyEvaluated = true;
        }
        argList.emplace_back(std::move(evaluatedArg));
      }

      if(anyEvaluated) {
        // Because some of the arguments have changed (they have been evaluated)
        // We create a new expression as a semi-evaluated one, and insert all the new arguments
        output = BulkComplexExpression(expression.getHead(), argList);
      }

      // still returning false, we did only a semi-evaluation
      return false;
    }
  }

  /// evaluate an argument from a batch batch
  /// doing multiple evaluation in a row if needed.
  /// Usually batchIndex == argIndex
  /// except in the case that we re-arrange a binary operator with 3+ arguments
  template <typename OperatorProperties>
  static BulkExpression evaluateExpressionArgument(BulkExpression const& expressionArgument,
                                                   size_t argIndex) {
    BulkExpression previousExpression;
    BulkExpression evaluatedExpression = expressionArgument;

    bool evaluated = true;
    bool hadTypeExpectedByTheOperator = false;
    // stop if we don't evaluate anymore
    while(evaluated) {
      // or if the batch type isn't compatible with the operator's argument type anymore
      bool hasTypeExpectedByTheOperator =
          OperatorProperties::isSupportedType(argIndex, evaluatedExpression);
      if(hadTypeExpectedByTheOperator && !hasTypeExpectedByTheOperator) {
        break;
      }

      previousExpression = evaluatedExpression;

      // little trick here until we can support overloading:
      // return as soon as we have compatible type
      // 1- until we find another way to pass symbol/batch to the db functions
      // 2- also for "Function" which are evaluated too early (when not applying the parameters)
      if(hadTypeExpectedByTheOperator) {
        break;
      }

      hadTypeExpectedByTheOperator = hasTypeExpectedByTheOperator;
      evaluated = Executor::evaluate(previousExpression, evaluatedExpression);
    }

    if(!evaluated) {
      // reached here if we stopped because it wasn't evaluating further
      if(!hadTypeExpectedByTheOperator) {
        // if the previous evaluation wasn't compatible with the operator's argument type
        // return the latest since we haven't anything better
        return evaluatedExpression;
      }
    }

    // in any other case, we return the latest evaluated batch
    return previousExpression;
  }
};
} // namespace boss::engines::bulk