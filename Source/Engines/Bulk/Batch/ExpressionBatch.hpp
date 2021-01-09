#pragma once

#include "CompoundBatch.hpp"

#include "../../../Expression.hpp"

#include <algorithm>
#include <memory>
#include <tuple>

namespace boss::engines::bulk {

// to get around template
// all Expression batches need to share a common class
template <typename... SupportedTypes>
class AnyExpressionBatch : public CompoundBatch<SupportedTypes...> {
public:
  using CompoundBatch = CompoundBatch<SupportedTypes...>;

  using ValueType = ComplexExpression;

  static constexpr UniqueId::type UniqueId = UniqueId::forType<AnyExpressionBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  AnyExpressionBatch(Symbol const& symbol, typename CompoundBatch::BatchList&& batches)
      : CompoundBatch(symbol, std::move(batches)) {}

  AnyExpressionBatch(AnyExpressionBatch const& other, bool clear = false)
      : CompoundBatch(other, clear) {}
};

template <typename EvaluatorType, typename Func, size_t FuncArgCount, bool FixedTypes,
          typename... SupportedTypes>
class ExpressionBatch : public AnyExpressionBatch<SupportedTypes...> {
public:
  using AnyExpressionBatch = AnyExpressionBatch<SupportedTypes...>;
  using CompoundBatch = CompoundBatch<SupportedTypes...>;

  using ArgumentList = typename CompoundBatch::BatchList;

  using ValueType = ComplexExpression;

  ExpressionBatch(EvaluatorType const& evaluator, ArgumentList&& batchArgs)
      : m_evaluator(evaluator),
        AnyExpressionBatch(Symbol(evaluator.getSymbol()), std::move(batchArgs)) {}

  ExpressionBatch(ExpressionBatch const& other, bool clear = false)
      : m_evaluator(other.m_evaluator), AnyExpressionBatch(other, clear) {}

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(new ExpressionBatch(*this, clear));
  }

  BatchPtr evaluate() const override { return evaluateHelper(); }

protected:
  EvaluatorType const& m_evaluator;

  // calls the evaluator with specific Batch types as arguments (not just generic Batch)
  template <typename InputBatchTuple, size_t... Indices>
  BatchPtr evaluateImpl(InputBatchTuple const& in, std::index_sequence<Indices...>) const {
    return m_evaluator((*std::get<Indices>(in))...);
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
      auto& batchPtr = this->m_batches[Index];
      auto evaluatedBatchPtr = evaluateHelper(Index, *batchPtr.get());

      BatchPtr outputBatchPtr;
      auto evaluateNext = [&, this](auto& batch) {
        using BatchType = std::decay_t<decltype(batch)>;
        if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
          outputBatchPtr =
              evaluateHelper<Index + 1>(std::make_tuple(static_cast<BatchType*>(&batch)));
        } else {
          outputBatchPtr = std::apply(
              [&, this](auto&&... arg) {
                return evaluateHelper<Index + 1>(
                    std::make_tuple((std::move(arg))..., static_cast<BatchType*>(&batch)));
              },
              batchTuple);
        }
      };

      if constexpr(FixedTypes) {
        EvaluatorType::template VisitExactType<Index>(evaluateNext, *evaluatedBatchPtr.get());
      } else {
        EvaluatorType::template VisitAllowedTypes(evaluateNext, *evaluatedBatchPtr.get());
      }

      if(!outputBatchPtr) {
        // the argument type was unsupported
        // but still apply what we already can evaluate
        ArgumentList argList;
        argList.reserve(FuncArgCount);
        // add previous evaluated arguments
        std::apply([&argList](auto&&... args) { (..., argList.emplace_back(std::move(args))); },
                   batchTuple);
        // add this current batch (as the state we were evaluating it)
        argList.emplace_back(std::move(evaluatedBatchPtr));
        // then finish to fill with all remaining args
        // still evaluate them as much as possible
        for(size_t index = Index + 1; index < FuncArgCount; ++index) {
          auto otherBatchPtr = evaluateHelper(index, *this->m_batches[index].get());
          argList.emplace_back(std::move(otherBatchPtr));
        }
        outputBatchPtr = BatchPtr(new ExpressionBatch(m_evaluator, std::move(argList)));
      }

      return outputBatchPtr;
    }
  }

  static BatchPtr evaluateHelper(size_t index, Batch& batch) {
    BatchPtr evaluatedBatchPtr;
    Batch* evaluatedBatch = &batch;
    while(true) { // do multiple evaluation in a row if needed
      auto currentTypeId = evaluatedBatch->typeId();

      // little trick here until we can support overloading
      // or find another way to pass symbol/tableView to the db functions
      // 2- also needed for "Function" which are evaluated too early (when not using parameters)
      bool needEvaluation = true;
      if constexpr(FixedTypes) {
        if(EvaluatorType::IsExactType(index, currentTypeId)) {
          needEvaluation = false;
        }
      } else {
        if(EvaluatorType::IsAllowedType(currentTypeId)) {
          needEvaluation = false;
        }
      }

      if(!needEvaluation) {
        break;
      }

      evaluatedBatchPtr = evaluatedBatch->evaluate();
      evaluatedBatch = evaluatedBatchPtr.get();

      if(evaluatedBatch->typeId() == currentTypeId) {
        break;
      }
    };

    if(!evaluatedBatchPtr) {
      return batch.clone();
    } else {
      return evaluatedBatchPtr;
    }
  }
};

} // namespace boss::engines::bulk
