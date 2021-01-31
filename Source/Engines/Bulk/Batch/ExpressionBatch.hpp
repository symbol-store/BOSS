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
      : CompoundBatch(factory, false, true, symbol) {}

  AnyExpressionBatch(AnyExpressionBatch const& other, bool clear = false)
      : CompoundBatch(other, clear) {}

  ~AnyExpressionBatch() override = default;
  AnyExpressionBatch(AnyExpressionBatch&& other) = delete;
  AnyExpressionBatch& operator=(AnyExpressionBatch const& other) = delete;
  AnyExpressionBatch& operator=(AnyExpressionBatch&& other) = delete;

  BatchPtr clone(bool clear = false) const override { return cloneAsAnyExpressionBatch(clear); }

  using CompoundBatch::CompoundBatchPtr;
  CompoundBatchPtr cloneAsCompoundBatch(bool clear = false) const override {
    return cloneAsAnyExpressionBatch(clear);
  }

  using AnyExpressionBatchPtr = std::unique_ptr<AnyExpressionBatch>;
  virtual AnyExpressionBatchPtr cloneAsAnyExpressionBatch(bool clear = false) const {
    return AnyExpressionBatchPtr(new AnyExpressionBatch(*this, clear));
  }
};

template <typename EvaluatorType, typename Func, size_t FuncArgCount>
class ExpressionBatch : public AnyExpressionBatch {
public:
  using ArgumentList = typename CompoundBatch::BatchList;

  using ValueType = AnyExpressionBatch::ValueType;

  ExpressionBatch(BatchFactory const& factory, EvaluatorType const& evaluator)
      : m_evaluator(evaluator), AnyExpressionBatch(factory, Symbol(evaluator.getSymbol())) {}

  ExpressionBatch(ExpressionBatch const& other, bool clear = false)
      : m_evaluator(other.m_evaluator), AnyExpressionBatch(other, clear) {}

  ~ExpressionBatch() override = default;
  ExpressionBatch(ExpressionBatch&& other) = delete;
  ExpressionBatch& operator=(ExpressionBatch const& other) = delete;
  ExpressionBatch& operator=(ExpressionBatch&& other) = delete;

  BatchPtr clone(bool clear = false) const override { return cloneAsExpressionBatch(clear); }

  using AnyExpressionBatch::AnyExpressionBatchPtr;
  AnyExpressionBatchPtr cloneAsAnyExpressionBatch(bool clear = false) const override {
    return cloneAsExpressionBatch(clear);
  }

  using ExpressionBatchPtr = std::unique_ptr<ExpressionBatch>;
  ExpressionBatchPtr cloneAsExpressionBatch(bool clear = false) const {
    return ExpressionBatchPtr(new ExpressionBatch(*this, clear));
  }

  void insert(ValueType const& expression) override {
    if constexpr(FuncArgCount == 2) {
      auto argsBegin = expression.getArguments().begin();
      auto argsEnd = expression.getArguments().end();
      auto sizeArgs = std::distance(argsBegin, argsEnd);
      if(sizeArgs > FuncArgCount) {
        // special case for binary operators
        // split arguments into pairs and create deeper compound expressions
        auto numPassingOverArgs = sizeArgs - 1;

        ExpressionArguments compoundList{argsBegin, std::next(argsBegin, numPassingOverArgs)};
        ComplexExpression compoundExpr{expression.getHead(), compoundList};

        ExpressionArguments newList{compoundExpr, *std::next(argsBegin, numPassingOverArgs)};
        ComplexExpression newExpr{expression.getHead(), newList};
        insert(newExpr);
        return;
      }
    }

    CompoundBatch::insert(expression);
  }

  BatchPtr evaluate() const override { return evaluateHelper(); }

private:
  EvaluatorType const& m_evaluator;

  // calls the evaluator with specific Batch types as arguments (not just generic Batch)
  template <typename InputBatchTuple, size_t... Indices>
  BatchPtr evaluateImpl(InputBatchTuple const& in,
                        std::index_sequence<Indices...> /*unused*/) const {
    return m_evaluator((*std::get<Indices>(in).get())...);
  }

  // build a tuple of specific Batch argument types
  // from dynamic information extracted from generic Batch list
  template <size_t Index = 0, typename... BatchTupleTypes>
  BatchPtr evaluateHelper(
      std::tuple<BatchTupleTypes...>&& batchTuple = std::tuple<BatchTupleTypes...>()) const {
    using BatchTuple = std::tuple<BatchTupleTypes...>;
    if constexpr(Index == FuncArgCount) {
      if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
        return m_evaluator();
      } else {
        return evaluateImpl(batchTuple, std::make_index_sequence<FuncArgCount>{});
      }
    } else {
      auto& batchPtr = *(begin() + Index);
      auto evaluatedBatchPtr = evaluateHelper(Index, *batchPtr);

      BatchPtr outputBatchPtr;
      auto evaluateNext = [&, this](auto& batch) {
        using BatchType = std::decay_t<decltype(batch)>;
        if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
          outputBatchPtr =
              evaluateHelper<Index + 1>(std::make_tuple(std::unique_ptr<BatchType>(&batch)));
        } else {
          outputBatchPtr = std::apply(
              [&, this](auto&&... arg) {
                return evaluateHelper<Index + 1>(
                    std::make_tuple((std::move(arg))..., std::unique_ptr<BatchType>(&batch)));
              },
              batchTuple);
        }
        // release it since its memory is already handled by the tuple of unique ptrs
        evaluatedBatchPtr.release(); // NOLINT
      };

      EvaluatorType::template visitExactType<Index>(evaluateNext, *evaluatedBatchPtr);

      if(outputBatchPtr) {
        return outputBatchPtr;
      }

      // the argument type was unsupported
      // but still apply what we already can evaluate
      ArgumentList argList;
      argList.reserve(FuncArgCount);
      // add previous evaluated arguments
      std::apply([&argList](auto&&... args) { (..., argList.emplace_back(std::move(args))); },
                 batchTuple);
      // add this current batch (at the state we were evaluating it)
      argList.emplace_back(std::move(evaluatedBatchPtr));
      // still evaluate them as much as possible
      for(size_t index = Index + 1; index < FuncArgCount; ++index) {
        auto otherBatchPtr = evaluateHelper(index, *(*(begin() + index)).get());
        argList.emplace_back(std::move(otherBatchPtr));
      }

      // create the new batch and insert the semi-evaluated batches
      auto partOutputBatchPtr = cloneAsCompoundBatch(true);
      auto& outputBatch = *partOutputBatchPtr;
      auto argIt = argList.begin();
      visitBatches([&outputBatch, &argIt](auto const& key, auto const& /*batch*/) {
        outputBatch.insert(key.first, key.second, std::move(*argIt));
        ++argIt;
      });

      return partOutputBatchPtr;
    }
  }

  static BatchPtr evaluateHelper(size_t index, Batch& batch) {
    BatchPtr previousBatchPtr;
    BatchPtr evaluatedBatchPtr;
    Batch* evaluatedBatch = &batch;
    bool isCorrectExpectedType = false;
    while(true) { // do multiple evaluation in a row if needed

      // little trick here until we can support overloading
      // or find another way to pass symbol/tableView to the db functions
      // 2- also needed for "Function" which are evaluated too early (when not using parameters)
      if(EvaluatorType::isExactType(index, *evaluatedBatch)) {
        isCorrectExpectedType = true;
      } else if(isCorrectExpectedType) {
        // go back to previous batch
        evaluatedBatchPtr = std::move(previousBatchPtr);
        break;
      }

      auto currentTypeId = evaluatedBatch->typeId();

      previousBatchPtr = std::move(evaluatedBatchPtr);
      evaluatedBatchPtr = evaluatedBatch->evaluate();
      evaluatedBatch = evaluatedBatchPtr.get();

      if(evaluatedBatch->typeId() == currentTypeId) {
        return evaluatedBatchPtr;
      }
    }

    if(evaluatedBatchPtr) {
      return evaluatedBatchPtr;
    }
    return batch.clone();
  }
};

} // namespace boss::engines::bulk
