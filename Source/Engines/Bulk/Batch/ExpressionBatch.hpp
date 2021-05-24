#pragma once

#include "CompoundBatch.hpp"

#include "../../../Expression.hpp"

#include <algorithm>
#include <memory>
#include <tuple>

namespace boss::engines::bulk {

/** AnyExpressionBatch is the base class for a batch
 * containing a complex expression to evaluate with an evaluator.
 * This base class is needed to have generic interface for all expression batches.
 * But then we derive to specific ExpressionBatch with templated lambda function to implement
 * evaluators. */
class AnyExpressionBatch : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<AnyExpressionBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  explicit AnyExpressionBatch(Symbol const& symbol) : CompoundBatch(symbol) {}

  AnyExpressionBatch(AnyExpressionBatch const& other, bool clear = false)
      : CompoundBatch(other, clear) {}

  ~AnyExpressionBatch() override = default;
  AnyExpressionBatch(AnyExpressionBatch&& other) = delete;
  AnyExpressionBatch& operator=(AnyExpressionBatch const& other) = delete;
  AnyExpressionBatch& operator=(AnyExpressionBatch&& other) = delete;

  Batch* clone(bool clear = false) const override { return cloneAsAnyExpressionBatch(clear); }

  CompoundBatch* cloneAsCompoundBatch(bool clear = false) const override {
    return cloneAsAnyExpressionBatch(clear);
  }

  virtual AnyExpressionBatch* cloneAsAnyExpressionBatch(bool clear = false) const {
    return new AnyExpressionBatch(*this, clear);
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, AnyExpressionBatch>, int> = 0>
  BatchType* cloneAs(bool clear = false) const {
    return cloneAsAnyExpressionBatch(clear);
  }

protected:
};

// [https://github.com/symbol-store/BOSS/issues/85] then we can get rid of this class
/** DeferredEvaluationBatch is a special case for an expression batch.
 * Instead of using an evaluator, we implement a specific evaluate function.
 * If we use an evaluator for it, it will force the argument to be evaluated first.
 */
class DeferredEvaluationBatch : public AnyExpressionBatch {
public:
  using ValueType = AnyExpressionBatch::ValueType;

  explicit DeferredEvaluationBatch(Symbol const& symbol) : AnyExpressionBatch(symbol) {}

  DeferredEvaluationBatch(DeferredEvaluationBatch const& other, bool clear = false)
      : AnyExpressionBatch(other, clear) {}

  ~DeferredEvaluationBatch() override = default;
  DeferredEvaluationBatch(DeferredEvaluationBatch&& other) = delete;
  DeferredEvaluationBatch& operator=(DeferredEvaluationBatch const& other) = delete;
  DeferredEvaluationBatch& operator=(DeferredEvaluationBatch&& other) = delete;

  Batch* clone(bool clear = false) const override { return cloneAsDeferredEvaluationBatch(clear); }

  AnyExpressionBatch* cloneAsAnyExpressionBatch(bool clear = false) const override {
    return cloneAsDeferredEvaluationBatch(clear);
  }
  virtual DeferredEvaluationBatch* cloneAsDeferredEvaluationBatch(bool clear = false) const {
    return new DeferredEvaluationBatch(*this, clear);
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, DeferredEvaluationBatch>, int> = 0>
  BatchType* cloneAs(bool clear = false) const {
    return cloneAsDeferredEvaluationBatch(clear);
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    // just return the argument without evaluating it
    outputPtr = *begin();
    return true;
  }
};

/** ExpressionBatch implements a specific lambda function as evaluator.
 * This is done at compile-time through templating,
 * so evaluate() can create a tree of compiled versions for the evaluator
 * for each combination of argument types. */
template <typename EvaluatorType, typename Func, size_t FuncArgCount>
class ExpressionBatch : public AnyExpressionBatch {
public:
  using ValueType = AnyExpressionBatch::ValueType;

  explicit ExpressionBatch(EvaluatorType const& evaluator)
      : AnyExpressionBatch(Symbol(evaluator.getSymbol())), m_evaluator(evaluator) {}

  ExpressionBatch(ExpressionBatch const& other, bool clear = false)
      : AnyExpressionBatch(other, clear), m_evaluator(other.m_evaluator) {}

  ~ExpressionBatch() override = default;
  ExpressionBatch(ExpressionBatch&& other) = delete;
  ExpressionBatch& operator=(ExpressionBatch const& other) = delete;
  ExpressionBatch& operator=(ExpressionBatch&& other) = delete;

  Batch* clone(bool clear = false) const override { return cloneAsExpressionBatch(clear); }

  AnyExpressionBatch* cloneAsAnyExpressionBatch(bool clear = false) const override {
    return cloneAsExpressionBatch(clear);
  }

  virtual ExpressionBatch* cloneAsExpressionBatch(bool clear = false) const {
    return new ExpressionBatch(*this, clear);
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, ExpressionBatch>, int> = 0>
  BatchType* cloneAs(bool clear = false) const {
    return cloneAsExpressionBatch(clear);
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    return buildArgumentsTupleAndEvaluate(outputPtr);
  }

private:
  /// the evaluator hold a generic lambda function
  /// which is expanded at compile-time to to match the type of the batch arguments
  EvaluatorType const& m_evaluator;

  /// calls the evaluator with specific Batch types as arguments (not just generic Batch)
  template <typename InputBatchTuple, size_t... Indices>
  Batch::ReadablePtr evaluateWithTypedArguments(InputBatchTuple&& in,
                                                std::index_sequence<Indices...> /*unused*/) const {
    return m_evaluator(std::get<Indices>(std::forward<InputBatchTuple>(in))...);
  }

  /// build a tuple of specific Batch argument types
  /// from dynamic information extracted from generic Batch list
  template <typename... ArgumentBatchTypes>
  bool buildArgumentsTupleAndEvaluate(ReadablePtr& outputPtr, size_t batchIndex = 0,
                                      std::tuple<ArgumentBatchTypes...>&& argumentsTuple =
                                          std::tuple<ArgumentBatchTypes...>()) const {
    using ArgumentsTuple = std::tuple<ArgumentBatchTypes...>;
    size_t constexpr ArgIndex = sizeof...(ArgumentBatchTypes);
    if constexpr(ArgIndex == FuncArgCount) {
      // We finish to build the argument batches
      // Now, we can pass it to the evaluator
      if constexpr(std::is_same_v<ArgumentsTuple, std::tuple<>>) {
        outputPtr = m_evaluator();
      } else {
        outputPtr = evaluateWithTypedArguments(std::move(argumentsTuple),
                                               std::make_index_sequence<FuncArgCount>{});
      }

      if constexpr(FuncArgCount == 2) {
        // Special case for binary operators: we can split arguments into pairs
        // to treat a longer argument list as a deeper compound expression.
        // This is needed because the evaluator arguments cannot be variadic
        // if we want them to be defined by the ExpressionBatch at compile time.
        if(batchIndex < numArguments()) {
          auto firstArgPtr = outputPtr;
          outputPtr.reset();
          bool visited = false;
          bool evaluated = false;
          EvaluatorType::template visitSupportedType<0>(
              [this, &firstArgPtr, &outputPtr, &visited, &evaluated,
               &batchIndex](auto const& typedBatch) {
                using BatchType = std::decay_t<decltype(typedBatch)>;
                ReadableBatchPtr<BatchType> typedBatchPtr(std::move(firstArgPtr));
                visited = true;
                evaluated = this->buildArgumentsTupleAndEvaluate(
                    outputPtr, batchIndex, std::forward_as_tuple(std::move(typedBatchPtr)));
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
      auto evaluatedPtr = getArgumentBatch(batchIndex, ArgIndex);

      bool visited = false;
      bool evaluated = false;
      outputPtr.reset();
      // ... get the specific type, add it to the tuple
      // and pass the new tuple to the same function recursively (compile-time recursion)
      EvaluatorType::template visitSupportedType<ArgIndex>(
          [this, &argumentsTuple, &evaluatedPtr, &outputPtr, &visited, &evaluated,
           &batchIndex](auto const& typedBatch) {
            using BatchType = std::decay_t<decltype(typedBatch)>;
            ReadableBatchPtr<BatchType> typedBatchPtr(std::move(evaluatedPtr));
            visited = true;
            evaluated = std::apply(
                [this, &typedBatchPtr, &outputPtr, batchIndex](auto... arg) {
                  // move evaluated ptr to the derived type
                  return this->buildArgumentsTupleAndEvaluate(
                      outputPtr, batchIndex + 1,
                      std::forward_as_tuple((std::move(arg))..., std::move(typedBatchPtr)));
                },
                std::move(argumentsTuple));
          },
          *evaluatedPtr);

      if(visited) {
        // If we reached here, it means that all the remaining args (from the recursion)
        // have been evaluated properly and the evaluator called with them
        // we can just return!
        return evaluated;
      }

      // We reached this portion of the code if an argument type isn't supported by the evaluator.
      // We need to build an output nevertheless, by evaluating as much as we can the remaining
      // arguments.
      // The argument tuple we built until here hasn't been consumed yet
      // so we can use it for constructing our output.

      std::vector<ReadablePtr> argList;
      argList.reserve(FuncArgCount);

      // add previous evaluated arguments
      std::apply([&argList](auto&&... args) { (..., argList.emplace_back(std::move(args))); },
                 argumentsTuple);

      // add this current batch (at the state we were evaluating it)
      argList.emplace_back(std::move(evaluatedPtr));

      // check if they anything has been evaluated with the previous arguments
      bool anyEvaluated = false;
      for(size_t i = 0; i < batchIndex; ++i) {
        auto beforePtr = *(begin() + i);
        if(argList[i].get() != beforePtr.get()) {
          anyEvaluated = true;
          break;
        }
      }

      // still evaluate remaining args as much as possible
      for(size_t i = batchIndex + 1; i < numArguments(); ++i) {
        auto argIndex = i < FuncArgCount ? i : FuncArgCount - 1;
        auto unevaluatedArgPtr = *(begin() + i);
        auto evaluatedArgPtr = getArgumentBatch(i, argIndex);
        if(evaluatedArgPtr.get() != unevaluatedArgPtr.get()) {
          anyEvaluated = true;
        }
        argList.emplace_back(std::move(evaluatedArgPtr));
      }

      if(anyEvaluated) {
        // Because some of the arguments have changed (they have been evaluated)
        // We create a new batch as a semi-evaluated one, and insert all the new arguments
        auto* partlyEvaluatedBatch = cloneAsCompoundBatch(true);
        partlyEvaluatedBatch->append(argList);
        outputPtr = WritableBatchPtr(partlyEvaluatedBatch);
      }

      // still returning false, we did only a semi-evaluation
      return false;
    }
  }

  /// Retrieve and evaluate an argument batch
  /// doing multiple evaluation in a row if needed.
  /// Usually batchIndex == argIndex
  /// except in the case that we re-arrange a binary operator with 3+ arguments
  Batch::ReadablePtr getArgumentBatch(size_t batchIndex, size_t argIndex) const {
    Batch::ReadablePtr previousBatchPtr;
    Batch::ReadablePtr evaluatedPtr = *(begin() + batchIndex);

    bool evaluated = true;
    bool hadTypeExpectedByTheEvaluator = false;
    // stop if we don't evaluate anymore
    while(evaluated) {
      // or if the batch type isn't compatible with the evaluator's argument type anymore
      bool hasTypeExpectedByTheEvaluator = EvaluatorType::isSupportedType(argIndex, *evaluatedPtr);
      if(hadTypeExpectedByTheEvaluator && !hasTypeExpectedByTheEvaluator) {
        break;
      }

      previousBatchPtr = evaluatedPtr;

      // little trick here until we can support overloading:
      // return as soon as we have compatible type
      // 1- until we find another way to pass symbol/tableView to the db functions
      // 2- also for "Function" which are evaluated too early (when not applying the parameters)
      if(hadTypeExpectedByTheEvaluator) {
        break;
      }

      hadTypeExpectedByTheEvaluator = hasTypeExpectedByTheEvaluator;
      evaluated = previousBatchPtr->evaluate(evaluatedPtr);
    }

    if(!evaluated) {
      // reached here if we stopped because it wasn't evaluating further
      if(evaluatedPtr) {
        // we have a partial evaluation at least...
        if(!hadTypeExpectedByTheEvaluator) {
          // ...and the previous evaluation wasn't compatible with the evaluator's argument type
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
