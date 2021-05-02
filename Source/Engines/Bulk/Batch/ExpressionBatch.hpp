#pragma once

#include "CompoundBatch.hpp"

#include "../../../Expression.hpp"

#include <algorithm>
#include <memory>
#include <tuple>

namespace boss::engines::bulk {

// to get around template
// all Expression batches need to share a common class
class AnyExpressionBatch : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<AnyExpressionBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  AnyExpressionBatch(BatchFactory const& factory, Symbol const& symbol)
      : CompoundBatch(factory, symbol) {}

  AnyExpressionBatch(AnyExpressionBatch const& other, bool clear = false)
      : CompoundBatch(other, clear) {}

  ~AnyExpressionBatch() override = default;
  AnyExpressionBatch(AnyExpressionBatch&& other) = delete;
  AnyExpressionBatch& operator=(AnyExpressionBatch const& other) = delete;
  AnyExpressionBatch& operator=(AnyExpressionBatch&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsAnyExpressionBatch(clear));
  }
  WritableBatchPtr<CompoundBatch> cloneAsCompoundBatch(bool clear = false) const override {
    return WritableBatchPtr<CompoundBatch>(cloneAsAnyExpressionBatch(clear));
  }
  virtual WritableBatchPtr<AnyExpressionBatch> cloneAsAnyExpressionBatch(bool clear = false) const {
    return WritableBatchPtr(new AnyExpressionBatch(*this, clear));
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, AnyExpressionBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsAnyExpressionBatch(clear);
  }
};

class UnevaluatedBatch : public AnyExpressionBatch {
public:
  using ValueType = AnyExpressionBatch::ValueType;

  UnevaluatedBatch(BatchFactory const& factory, Symbol const& symbol)
      : AnyExpressionBatch(factory, symbol) {}

  UnevaluatedBatch(UnevaluatedBatch const& other, bool clear = false)
      : AnyExpressionBatch(other, clear) {}

  ~UnevaluatedBatch() override = default;
  UnevaluatedBatch(UnevaluatedBatch&& other) = delete;
  UnevaluatedBatch& operator=(UnevaluatedBatch const& other) = delete;
  UnevaluatedBatch& operator=(UnevaluatedBatch&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsUnevaluatedBatch(clear));
  }
  WritableBatchPtr<AnyExpressionBatch>
  cloneAsAnyExpressionBatch(bool clear = false) const override {
    return WritableBatchPtr<AnyExpressionBatch>(cloneAsUnevaluatedBatch(clear));
  }
  virtual WritableBatchPtr<UnevaluatedBatch> cloneAsUnevaluatedBatch(bool clear = false) const {
    return WritableBatchPtr(new UnevaluatedBatch(*this, clear));
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, UnevaluatedBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsUnevaluatedBatch(clear);
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    outputPtr = *begin();
    return true;
  }
};

template <typename EvaluatorType, typename Func, size_t FuncArgCount>
class ExpressionBatch : public AnyExpressionBatch {
public:
  using ValueType = AnyExpressionBatch::ValueType;

  ExpressionBatch(BatchFactory const& factory, EvaluatorType const& evaluator)
      : AnyExpressionBatch(factory, Symbol(evaluator.getSymbol())), m_evaluator(evaluator) {}

  ExpressionBatch(ExpressionBatch const& other, bool clear = false)
      : AnyExpressionBatch(other, clear), m_evaluator(other.m_evaluator) {}

  ~ExpressionBatch() override = default;
  ExpressionBatch(ExpressionBatch&& other) = delete;
  ExpressionBatch& operator=(ExpressionBatch const& other) = delete;
  ExpressionBatch& operator=(ExpressionBatch&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsExpressionBatch(clear));
  }
  WritableBatchPtr<AnyExpressionBatch>
  cloneAsAnyExpressionBatch(bool clear = false) const override {
    return WritableBatchPtr<AnyExpressionBatch>(cloneAsExpressionBatch(clear));
  }
  virtual WritableBatchPtr<ExpressionBatch> cloneAsExpressionBatch(bool clear = false) const {
    return WritableBatchPtr(new ExpressionBatch(*this, clear));
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, ExpressionBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsExpressionBatch(clear);
  }

  void insert(ValueType const& expression) override {
    if constexpr(FuncArgCount == 2) {
      auto argsBegin = expression.getArguments().begin();
      auto argsEnd = expression.getArguments().end();
      auto sizeArgs = std::distance(argsBegin, argsEnd);
      if(sizeArgs > FuncArgCount) {
        // special case for binary operators
        // split arguments into pairs and create deeper compound expressions
        ExpressionArguments newArgList(argsBegin, std::next(argsBegin, FuncArgCount));
        for(int argIndex = FuncArgCount; argIndex < sizeArgs; ++argIndex) {
          ComplexExpression compoundExpr{expression.getHead(), newArgList};
          ExpressionArguments compoundList({compoundExpr, *std::next(argsBegin, argIndex)});
          newArgList.swap(compoundList);
        }

        insert(ComplexExpression{expression.getHead(), newArgList});
        return;
      }
    }

    CompoundBatch::insert(expression);
  }

  bool evaluate(ReadablePtr& outputPtr) const override { return evaluateHelper(outputPtr); }

private:
  EvaluatorType const& m_evaluator;

  // calls the evaluator with specific Batch types as arguments (not just generic Batch)
  template <typename InputBatchTuple, size_t... Indices>
  Batch::ReadablePtr evaluateImpl(InputBatchTuple&& in,
                                  std::index_sequence<Indices...> /*unused*/) const {
    return m_evaluator(std::get<Indices>(std::forward<InputBatchTuple>(in))...);
  }

  // build a tuple of specific Batch argument types
  // from dynamic information extracted from generic Batch list
  template <size_t Index = 0, typename... BatchTupleTypes>
  bool evaluateHelper(ReadablePtr& outputPtr, std::tuple<BatchTupleTypes...>&& batchTuple =
                                                  std::tuple<BatchTupleTypes...>()) const {
    using BatchTuple = std::tuple<BatchTupleTypes...>;
    if constexpr(Index == FuncArgCount) {
      if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
        outputPtr = m_evaluator();
      } else {
        outputPtr = evaluateImpl(std::move(batchTuple), std::make_index_sequence<FuncArgCount>{});
      }
      return true;
    } else {
      auto batchIt = begin() + Index;
      auto batchPtr = *batchIt;
      auto evaluatedPtr = evaluateHelper(Index, batchPtr);

      bool visited = false;
      bool evaluated = false;
      outputPtr.reset();
      EvaluatorType::template visitExactType<Index>(
          [this, &batchTuple, &evaluatedPtr, &outputPtr, &visited,
           &evaluated](auto const& specificBatch) {
            using BatchType = std::decay_t<decltype(specificBatch)>;
            ReadableBatchPtr<BatchType> specificBatchPtr(std::move(evaluatedPtr));
            visited = true;
            evaluated = std::apply(
                [&specificBatchPtr, &outputPtr, this](auto... arg) {
                  // move evaluated ptr to the derived type
                  return this->evaluateHelper<Index + 1>(
                      outputPtr,
                      std::forward_as_tuple((std::move(arg))..., std::move(specificBatchPtr)));
                },
                std::move(batchTuple));
          },
          *evaluatedPtr);

      if(visited) {
        return evaluated;
      }

      // the argument type was unsupported
      // but still apply what we already can evaluate
      std::vector<ReadablePtr> argList;
      argList.reserve(FuncArgCount);

      // add previous evaluated arguments
      std::apply([&argList](auto&&... args) { (..., argList.emplace_back(std::move(args))); },
                 batchTuple);

      // add this current batch (at the state we were evaluating it)
      argList.emplace_back(std::move(evaluatedPtr));

      // check if they anything has been evaluated
      bool anyEvaluated = false;
      for(size_t index = 0; index < Index; ++index) {
        auto beforePtr = *(begin() + index);
        if(argList[index].get() != beforePtr.get()) {
          anyEvaluated = true;
          break;
        }
      }

      // still evaluate remaining args as much as possible
      for(size_t index = Index + 1; index < FuncArgCount; ++index) {
        auto otherPtr = *(begin() + index);
        auto otherEvaluatedPtr = evaluateHelper(index, otherPtr);
        if(otherEvaluatedPtr.get() != otherPtr.get()) {
          anyEvaluated = true;
        }
        argList.emplace_back(std::move(otherEvaluatedPtr));
      }

      if(!anyEvaluated) {
        return false;
      }

      // create the new batch and insert the semi-evaluated batches
      auto partlyEvaluatedPtr = cloneAsCompoundBatch(true);
      partlyEvaluatedPtr->insert(argList);
      outputPtr = std::move(partlyEvaluatedPtr);

      // still return false, we did only semi-evaluation
      return false;
    }
  }

  static Batch::ReadablePtr evaluateHelper(size_t index, Batch::ReadablePtr const& batchPtr) {
    Batch::ReadablePtr previousPtr;
    Batch::ReadablePtr evaluatedPtr = batchPtr;
    bool isCorrectExpectedType = false;
    while(true) { // do multiple evaluation in a row if needed
      auto const& evaluatedBatch = *evaluatedPtr;

      // little trick here until we can support overloading
      // or find another way to pass symbol/tableView to the db functions
      // 2- also needed for "Function" which are evaluated too early (when not using parameters)
      if(EvaluatorType::isExactType(index, evaluatedBatch)) {
        isCorrectExpectedType = true;
        if(previousPtr) { // ok if already evaluated at least once
          break;
        }
      } else if(isCorrectExpectedType) {
        // go back to previous batch
        return previousPtr;
      }

      previousPtr = std::move(evaluatedPtr);
      bool evaluated = evaluatedBatch.evaluate(evaluatedPtr);

      if(!evaluated) {
        if(evaluatedPtr && !isCorrectExpectedType) {
          // still return a partial evaluation if we have nothing better
          return evaluatedPtr;
        }
        return previousPtr;
      }
    }

    return evaluatedPtr;
  }
};

} // namespace boss::engines::bulk
