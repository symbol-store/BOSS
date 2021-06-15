#pragma once

#include "BulkExpression.hpp"
#include "OperatorRegistry.hpp"

namespace boss::engines::bulk {

/** This is the main evaluate() function to call for any bulk expression.
 * However, for anything else than complex expression and symbols, it will keep them as they are.
 * It also contains the logic for:
 * - the evaluation of the arguments (from the leaves to the root)
 * - the resolution of the argument types, making sure it matches the operator's expected types,
 * and passing the list of (typed) arguments to the operator's evaluate() function
 */
class Executor {
public:
  static BulkExpression evaluate(BulkExpression const& expression) {
    return OperatorRegistry<Executor>::instance().findAndExecuteOperator(expression);
  }

  /// This is the function called back from the operator registry,
  /// receiving the specific operator with which to call evaluate
  template <typename OperatorType>
  static BulkExpression execute(OperatorType const& op, BulkComplexExpression const& expression) {
    return buildArgumentsTupleAndEvaluate(op, expression);
  }

private:
  /// calls the evaluation function with specific argument types
  /// (matching the operator's parameter types)
  template <typename OperatorType, typename ArgumentsTuple, size_t... Indices>
  static BulkExpression evaluateWithTypedArguments(OperatorType const& op, ArgumentsTuple&& in,
                                                   std::index_sequence<Indices...> /*unused*/) {
    return op.evaluate(std::get<Indices>(std::forward<ArgumentsTuple>(in))...);
  }

  /// build a tuple of specific argument types from the generic expression arguments
  /// and send it to the operator for evaluation (if they match with the parameter types)
  template <typename OperatorType, typename... ArgumentTypes>
  static BulkExpression
  buildArgumentsTupleAndEvaluate(OperatorType const& op, BulkComplexExpression const& expression,
                                 size_t argIndex = 0,
                                 std::tuple<ArgumentTypes...>&& argumentsTuple = std::tuple<>()) {
    using ArgumentsTuple = std::tuple<ArgumentTypes...>;
    using OperatorProperties = typename OperatorType::Properties;
    size_t constexpr parameterCount = OperatorProperties::ParameterCount;
    size_t constexpr parameterIndex = sizeof...(ArgumentTypes); // how many we evaluated so far
    if constexpr(parameterIndex == parameterCount) {
      // We finish to evaluate and check all the arguments
      // Now, we can pass it to the operator
      BulkExpression output;
      if constexpr(std::is_same_v<ArgumentsTuple, std::tuple<>>) {
        output = op.evaluate();
      } else {
        output = evaluateWithTypedArguments(op, std::move(argumentsTuple),
                                            std::make_index_sequence<parameterCount>{});
      }

      if constexpr(parameterCount == 2) {
        // Special case for binary operators: we can split arguments into pairs
        // to treat a longer argument list as a deeper compound expression.
        // This is needed because the arguments of the evaluation cannot be variadic
        // if we want them to be defined by the Operator at compile time.
        // We can get rid of it if the evaluation is not a lambda function anymore
        if(argIndex < expression.getArguments().size()) {
          auto firstArg = output;
          bool visited = OperatorProperties::template visitSupportedType<0>(
              [&output, &op, expression, &argIndex](auto const& typedArg) {
                output = buildArgumentsTupleAndEvaluate(op, expression, argIndex,
                                                        std::forward_as_tuple(typedArg));
              },
              firstArg);
          if(visited) {
            return output;
          }
          // Otherwise, the output type wasn't a compatible argument type.
          // In that case, put back the initial output
          // it will just ignore the remaining arguments
          output = firstArg;
        }
      }
      return output;
    } else {
      // Here is the main part of the function
      // Evaluate and check the next argument...
      auto const& unevaluatedArg = expression.getArguments()[argIndex];
      auto evaluatedArg =
          evaluateCandidateArgument<OperatorProperties>(unevaluatedArg, parameterIndex);

      BulkExpression output;
      // ... get the specific type, add it to the tuple
      // and pass the new tuple to the same function recursively (compile-time recursion)
      bool visited = OperatorProperties::template visitSupportedType<parameterIndex>(
          [&argumentsTuple, &output, &op, &expression, &argIndex](auto const& typedArgument) {
            std::apply(
                [&typedArgument, &output, &op, &expression, &argIndex](auto... arg) {
                  // move evaluated ptr to the derived type
                  output = buildArgumentsTupleAndEvaluate(
                      op, expression, argIndex + 1,
                      std::forward_as_tuple((std::move(arg))..., typedArgument));
                },
                std::move(argumentsTuple));
          },
          evaluatedArg);

      if(visited) {
        // If we reached here, it means that all the remaining args (from the recursion)
        // have been evaluated properly and the operator's evaluation called with them.
        return output;
      }

      // We reached this portion of the code if an argument type is not supported by the operator.
      // We need to build an output nevertheless, by evaluating the remaining arguments
      // as much as we can.

      BulkExpressionArguments argList;
      argList.reserve(parameterCount);

      // add previous evaluated arguments
      std::apply([&argList](auto&&... args) { (..., argList.emplace_back(std::move(args))); },
                 argumentsTuple);

      // add this current argument (at the stage we were evaluating it)
      argList.emplace_back(std::move(evaluatedArg));

      // check if they anything has been evaluated with the previous arguments
      bool anyEvaluated = false;
      for(size_t i = 0; i < argIndex; ++i) {
        auto const& beforeArg = expression.getArguments()[i];
        if(argList[i] != beforeArg) {
          anyEvaluated = true;
          break;
        }
      }

      // still evaluate remaining args as much as possible
      for(size_t i = argIndex + 1; i < expression.getArguments().size(); ++i) {
        auto paramIndex = i < parameterCount ? i : parameterCount - 1;
        auto const& unevalArg = expression.getArguments()[i];
        auto evalArg = evaluateCandidateArgument<OperatorProperties>(unevalArg, paramIndex);
        if(evalArg != unevalArg) {
          anyEvaluated = true;
        }
        argList.emplace_back(std::move(evalArg));
      }

      if(anyEvaluated) {
        // Because some of the arguments have changed (they have been evaluated)
        // We create a new expression as a semi-evaluated one, and insert all the new arguments
        return BulkComplexExpression(expression.getHead(), argList);
      }

      return expression;
    }
  }

  /// Evaluate a candidate argument to resolve it as the operator's nth parameter
  /// We are doing multiple evaluation of it in a row if needed.
  template <typename OperatorProperties>
  static BulkExpression evaluateCandidateArgument(BulkExpression const& candidate,
                                                  size_t parameterIndex) {
    if constexpr(OperatorProperties::argEvaluation == ArgumentEvaluationMethod::HOLD_ALL) {
      return candidate;
    }

    BulkExpression previousExpression;
    BulkExpression evaluatedExpression = candidate;

    bool hadTypeExpectedByTheOperator = false;

    // stop if we don't evaluate anymore
    while(evaluatedExpression != previousExpression) {
      bool hasTypeExpectedByTheOperator =
          OperatorProperties::isSupportedType(parameterIndex, evaluatedExpression);

      if constexpr(OperatorProperties::argEvaluation == ArgumentEvaluationMethod::MAX_EVALUATION) {
        // also check if the evaluated type is still a compatible argument
        if(hadTypeExpectedByTheOperator && !hasTypeExpectedByTheOperator) {
          break;
        }
      }

      if constexpr(OperatorProperties::argEvaluation == ArgumentEvaluationMethod::UNTIL_MATCHES) {
        // if we don't do full evaluation, just return as soon as it is compatible
        if(hasTypeExpectedByTheOperator) {
          break;
        }
      }

      previousExpression = evaluatedExpression;
      evaluatedExpression = Executor::evaluate(previousExpression);

      hadTypeExpectedByTheOperator = hasTypeExpectedByTheOperator;

      if constexpr(OperatorProperties::argEvaluation == ArgumentEvaluationMethod::ONLY_ONCE) {
        break;
      }
    }

    if constexpr(OperatorProperties::argEvaluation == ArgumentEvaluationMethod::MAX_EVALUATION) {
      if(hadTypeExpectedByTheOperator) {
        // if we are here, it means that the evaluated wasn't compatible anymore
        // better return the previous one
        return previousExpression;
      }
    }

    // in any other case, we return the latest evaluated argument
    return evaluatedExpression;
  }
};
} // namespace boss::engines::bulk