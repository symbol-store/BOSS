#pragma once

#include "BatchVisitDispatcher.hpp"
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
  static bool evaluate(Batch const& batch, Batch::ReadablePtr& outputPtr) {
    outputPtr.reset();
    return OperatorRegistry<Executor>::instance().findAndExecuteOperator(batch, outputPtr);
  }

  /// evaluate with parameters
  /// assume the batch to be composed of an optional parameter list and a body
  static bool evaluate(CompoundBatch const& functionBatch,
                       std::vector<Batch::ReadablePtr> const& args, Batch::ReadablePtr& outputPtr) {
    if(functionBatch.numArguments() == 0) {
      // the function is the body?
      // we can just do a normal evaluation, but it should probably not happen...
      return evaluate(functionBatch, outputPtr);
    }

    if(functionBatch.numArguments() < 2) {
      // the predicate has only a body (not dependent on tuple)
      // we can just ignore the parameters and evaluate the body
      auto bodyPtr = *functionBatch.begin();
      if(!evaluate(*bodyPtr, outputPtr)) {
        // if it fails to evaluate, return the body itself
        outputPtr = std::move(bodyPtr);
      }
      return true;
    }

    auto functionBatchIt = functionBatch.begin();
    auto parametersPtr = *functionBatchIt;
    auto bodyPtr = *(functionBatchIt + 1);

    auto const& parametersBatch = *parametersPtr;
    auto const& bodyBatch = *bodyPtr;

    using SymbolPtr = DefaultSymbolRegistry::SymbolPtr;
    std::vector<std::pair<SymbolPtr&, SymbolPtr>> oldSymbols;
    oldSymbols.reserve(args.size());
    auto registerArgument = [&oldSymbols](Symbol const& parameter, auto const& valuePtr) {
      // store existing symbols
      // to retrieve later in case of name collision
      // (and make sure they are not destroyed while dereferenced...)
      auto& batchPtr = DefaultSymbolRegistry::instance().findSymbol(parameter);
      oldSymbols.emplace_back(batchPtr, std::move(batchPtr));

      // set symbol at the function scope
      batchPtr = valuePtr;
    };

    // replace parameter symbols with arguments
    // by iterating on both the parameter batch and arg list
    BatchVisitDispatcher<CompoundBatch, SymbolBatch>::visit(
        [&args, &registerArgument](auto const& typedBatch) {
          using BatchType = std::decay_t<decltype(typedBatch)>;
          auto argIt = args.begin();
          auto parameterIt = typedBatch.begin();
          for(; argIt != args.end() && parameterIt != typedBatch.end(); ++argIt, ++parameterIt) {
            if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
              auto symbolBatchPtr = *parameterIt;
              if(symbolBatchPtr->typeId() == UniqueId::forType<SymbolBatch>()) {
                auto const& symbolBatch = static_cast<SymbolBatch const&>(*symbolBatchPtr);
                registerArgument(*symbolBatch.begin(), *argIt);
              }
            } else { // as SymbolBatch
              registerArgument(*parameterIt, *argIt);
            }
          }
        },
        parametersBatch);

    bool evaluated = evaluate(bodyBatch, outputPtr);

    // before finishing, set back any colliding symbol (or clear them)
    for(auto& oldSymbol : oldSymbols) {
      oldSymbol.first = std::move(oldSymbol.second);
    }

    return evaluated;
  }

  /// This is the function called back from the operator registry,
  /// receiving the specific operator type to call evaluate()
  template <typename OperatorType>
  static auto execute(Batch::ReadablePtr& outputPtr, OperatorType const& op,
                      CompoundBatch const& batch) {
    return buildArgumentsTupleAndEvaluate(outputPtr, op, batch);
  }

private:
  /// build a tuple of specific Batch argument types
  /// from dynamic information extracted from generic Batch list
  template <typename OperatorType, typename... ArgumentBatchTypes>
  static bool buildArgumentsTupleAndEvaluate(
      Batch::ReadablePtr& outputPtr, OperatorType const& op, CompoundBatch const& batch,
      size_t batchIndex = 0, std::tuple<ArgumentBatchTypes...>&& argumentsTuple = std::tuple<>()) {
    using ArgumentsTuple = std::tuple<ArgumentBatchTypes...>;
    using OperatorProperties = typename OperatorType::Properties;
    size_t constexpr FuncArgCount = OperatorProperties::ArgumentCount;
    size_t constexpr ArgIndex = sizeof...(ArgumentBatchTypes);
    if constexpr(ArgIndex == FuncArgCount) {
      // We finish to build the argument batches
      // Now, we can pass it to the operator
      if constexpr(std::is_same_v<ArgumentsTuple, std::tuple<>>) {
        outputPtr = op.evaluate();
      } else {
        outputPtr =
            op.evaluate(std::move(argumentsTuple), std::make_index_sequence<FuncArgCount>{});
      }

      if constexpr(FuncArgCount == 2) {
        // Special case for binary operators: we can split arguments into pairs
        // to treat a longer argument list as a deeper compound expression.
        // This is needed because the arguments of the evaluation cannot be variadic
        // if we want them to be defined by the Operator at compile time.
        // We can get rid of it once the evaluation is not a lambda function anymore
        if(batchIndex < batch.numArguments()) {
          auto firstArgPtr = outputPtr;
          outputPtr.reset();
          bool visited = false;
          bool evaluated = false;
          OperatorProperties::template visitSupportedType<0>(
              [&firstArgPtr, &outputPtr, &visited, &evaluated, &op, batch,
               &batchIndex](auto const& typedBatch) {
                using BatchType = std::decay_t<decltype(typedBatch)>;
                ReadableBatchPtr<BatchType> typedBatchPtr(std::move(firstArgPtr));
                visited = true;
                evaluated =
                    buildArgumentsTupleAndEvaluate(outputPtr, op, batch, batchIndex,
                                                   std::forward_as_tuple(std::move(typedBatchPtr)));
              },
              *firstArgPtr);
          if(visited) {
            return evaluated;
          }
          // Otherwise, the output type wasn't a compatible argument type.
          // In that case, put back the initial output
          // it will just ignore the remaining arguments
          // [https://github.com/symbol-store/BOSS/issues/87] probably related with it
          // better returning full arguments but unevaluated expression
          outputPtr = std::move(firstArgPtr);
        }
      }
      return true;
    } else {
      // Here is the main part of the function
      // Build the next argument batch...
      auto evaluatedPtr =
          evaluateArgumentFromBatch<OperatorProperties>(batch, batchIndex, ArgIndex);

      bool visited = false;
      bool evaluated = false;
      outputPtr.reset();
      // ... get the specific type, add it to the tuple
      // and pass the new tuple to the same function recursively (compile-time recursion)
      OperatorProperties::template visitSupportedType<ArgIndex>(
          [&argumentsTuple, &evaluatedPtr, &outputPtr, &visited, &evaluated, &op, &batch,
           &batchIndex](auto const& typedBatch) {
            using BatchType = std::decay_t<decltype(typedBatch)>;
            ReadableBatchPtr<BatchType> typedBatchPtr(std::move(evaluatedPtr));
            visited = true;
            evaluated = std::apply(
                [&typedBatchPtr, &outputPtr, &op, &batch, &batchIndex](auto... arg) {
                  // move evaluated ptr to the derived type
                  return buildArgumentsTupleAndEvaluate(
                      outputPtr, op, batch, batchIndex + 1,
                      std::forward_as_tuple((std::move(arg))..., std::move(typedBatchPtr)));
                },
                std::move(argumentsTuple));
          },
          *evaluatedPtr);

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

      std::vector<Batch::ReadablePtr> argList;
      argList.reserve(FuncArgCount);

      // add previous evaluated arguments
      std::apply([&argList](auto&&... args) { (..., argList.emplace_back(std::move(args))); },
                 argumentsTuple);

      // add this current batch (at the state we were evaluating it)
      argList.emplace_back(std::move(evaluatedPtr));

      // check if they anything has been evaluated with the previous arguments
      bool anyEvaluated = false;
      for(size_t i = 0; i < batchIndex; ++i) {
        auto beforePtr = *(batch.begin() + i);
        if(argList[i].get() != beforePtr.get()) {
          anyEvaluated = true;
          break;
        }
      }

      // still evaluate remaining args as much as possible
      for(size_t i = batchIndex + 1; i < batch.numArguments(); ++i) {
        auto argIndex = i < FuncArgCount ? i : FuncArgCount - 1;
        auto unevaluatedArgPtr = *(batch.begin() + i);
        auto evaluatedArgPtr = evaluateArgumentFromBatch<OperatorProperties>(batch, i, argIndex);
        if(evaluatedArgPtr.get() != unevaluatedArgPtr.get()) {
          anyEvaluated = true;
        }
        argList.emplace_back(std::move(evaluatedArgPtr));
      }

      if(anyEvaluated) {
        // Because some of the arguments have changed (they have been evaluated)
        // We create a new batch as a semi-evaluated one, and insert all the new arguments
        auto* partlyEvaluatedBatch = batch.cloneAsCompoundBatch(true);
        partlyEvaluatedBatch->clear(); // clear empty builder too
        partlyEvaluatedBatch->append(argList);
        outputPtr = WritableBatchPtr(partlyEvaluatedBatch);
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
  static Batch::ReadablePtr evaluateArgumentFromBatch(CompoundBatch const& batch, size_t batchIndex,
                                                      size_t argIndex) {
    Batch::ReadablePtr previousBatchPtr;
    Batch::ReadablePtr evaluatedPtr = *(batch.begin() + batchIndex);

    bool evaluated = true;
    bool hadTypeExpectedByTheOperator = false;
    // stop if we don't evaluate anymore
    while(evaluated) {
      // or if the batch type isn't compatible with the operator's argument type anymore
      bool hasTypeExpectedByTheOperator =
          OperatorProperties::isSupportedType(argIndex, *evaluatedPtr);
      if(hadTypeExpectedByTheOperator && !hasTypeExpectedByTheOperator) {
        break;
      }

      previousBatchPtr = evaluatedPtr;

      // little trick here until we can support overloading:
      // return as soon as we have compatible type
      // 1- until we find another way to pass symbol/batch to the db functions
      // 2- also for "Function" which are evaluated too early (when not applying the parameters)
      if(hadTypeExpectedByTheOperator) {
        break;
      }

      hadTypeExpectedByTheOperator = hasTypeExpectedByTheOperator;
      evaluated = Executor::evaluate(*previousBatchPtr, evaluatedPtr);
    }

    if(!evaluated) {
      // reached here if we stopped because it wasn't evaluating further
      if(evaluatedPtr) {
        // we have a partial evaluation at least...
        if(!hadTypeExpectedByTheOperator) {
          // ...and the previous evaluation wasn't compatible with the operator's argument type
          // so return the latest since we haven't anything better
          return evaluatedPtr;
        }
      }
    }

    // in any other case, we return the latest evaluated batch
    return previousBatchPtr;
  }
};
} // namespace boss::engines::bulk