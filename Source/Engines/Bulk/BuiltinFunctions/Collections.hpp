#pragma once

#include "../Batch/ValueBatch.hpp"
#include "../BatchVisitDispatcher.hpp"
#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Collections {
  using AnySimpleBatch = typename OperatorUtils::AnySimpleBatch;
  using NonSymbolicBatch = typename OperatorUtils::NonSymbolicBatch;

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<ExtractOperator>("Extract");
    operatorRegistry.template registerOperator<FirstOperator>("First");
    operatorRegistry.template registerOperator<LastOperator>("Last");
    operatorRegistry.template registerOperator<ColumnOperator>("Column");
    operatorRegistry.template registerOperator<LengthOperator>("Length");
    operatorRegistry.template registerOperator<IndexOfOperator>("IndexOf");
  }

private:
  class ExtractOperator : public Operator<2, NonSymbolicBatch, AllowedBatches<ValueBatch<int>>> {
  public:
    template <typename ExprType, typename IndexType>
    auto evaluate(ExprType&& exprBatchesPtr, IndexType&& indexBatchPtr) const {
      auto extraction = [](auto const& exprBatch, size_t index) {
        using BatchType = std::decay_t<decltype(exprBatch)>;
        using ValueType = typename BatchType::ValueType;
        if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
          return exprBatch.extract(index);
        } else {
          auto const& value = static_cast<ValueType>(*(exprBatch.begin() + index));
          return Batch::WritablePtr(new ValueBatch(1, value));
        }
      };

      auto const& exprBatches = *exprBatchesPtr;
      auto const& indexBatch = *indexBatchPtr;
      if(indexBatch.size() == 1) {
        // special case for single batch extraction
        size_t index = *indexBatch.begin() - 1U;
        return Batch::ReadablePtr(extraction(exprBatches, index));
      }
      // general case for multiple extraction
      // exprBatchesPtr should be a compound
      using BatchType = std::decay_t<decltype(exprBatches)>;
      auto compoundBatchPtr = WritableBatchPtr(exprBatches.template cloneAs<BatchType>(true));
      if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
        auto& compoundBatch = *compoundBatchPtr;
        auto indexIt = indexBatch.begin();
        auto exprIt = exprBatches.begin();
        std::vector<Batch::ReadablePtr> argBatches;
        argBatches.reserve(indexBatch.size());
        for(; indexIt != indexBatch.end() && exprIt != exprBatches.end(); ++indexIt, ++exprIt) {
          size_t index = *indexIt - 1U;
          auto exprBatchPtr = *exprIt;
          OperatorUtils::AnyBatchVisitDispatcher::visit(
              [&index, &argBatches, &extraction](auto const& exprBatch) {
                argBatches.emplace_back(extraction(exprBatch, index));
              },
              *exprBatchPtr);
        }
        compoundBatch.append(argBatches);
      }
      return Batch::ReadablePtr(std::move(compoundBatchPtr));
    }
  };

  class FirstOperator : public Operator<1, NonSymbolicBatch> {
  public:
    template <typename BatchPtrType> auto evaluate(BatchPtrType&& batchPtrExpr) const {
      using BatchType = typename BatchPtrType::BatchType;
      using ValueType = typename BatchType::ValueType;
      if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
        return batchPtrExpr->extract(0);
      } else {
        auto const& value = static_cast<ValueType>(*batchPtrExpr->begin());
        return Batch::WritablePtr(new ValueBatch(1, value));
      }
    }
  };

  class LastOperator : public Operator<1, NonSymbolicBatch> {
  public:
    template <typename BatchPtrType> auto evaluate(BatchPtrType&& batchPtrExpr) const {
      using BatchType = typename BatchPtrType::BatchType;
      using ValueType = typename BatchType::ValueType;
      size_t index = batchPtrExpr->size() - 1;
      if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
        return batchPtrExpr->extract(index);
      } else {
        auto const& value = static_cast<ValueType>(*(batchPtrExpr->begin() + index));
        return Batch::WritablePtr(new ValueBatch(1, value));
      }
    }
  };

  class ColumnOperator
      : public Operator<2, AllowedBatches<CompoundBatch>, AllowedBatches<ValueBatch<int>>> {
  public:
    template <typename ExprType, typename IndexType>
    auto evaluate(ExprType&& exprBatchesPtr, IndexType&& indexBatchPtr) const {
      size_t index = *indexBatchPtr->begin() - 1;
      return exprBatchesPtr->column(index);
    }
  };

  class LengthOperator : public Operator<1, NonSymbolicBatch> {
  public:
    template <typename BatchType> auto evaluate(BatchType&& batchPtr) const {
      int value = batchPtr->size();
      return Batch::WritablePtr(new ValueBatch(1, value));
    }
  };

  class IndexOfOperator : public Operator<2, AllowedBatches<CompoundBatch>, AnySimpleBatch> {
  public:
    template <typename ListType, typename ValueType>
    auto evaluate(ListType&& listBatchPtr, ValueType&& valueBatchPtr) const {
      auto const& valueBatch = *valueBatchPtr;
      using ValueBatchType = std::decay_t<decltype(valueBatch)>;
      int index = 1;
      auto const& value = *valueBatchPtr->begin();
      for(auto argBatchPtr : *listBatchPtr) {
        // check for equality on the same type only
        bool equals = false;
        BatchVisitDispatcher<ValueBatchType>::visit(
            [&equals, &value](auto& argBatchTyped) {
              if(*argBatchTyped.begin() == value) {
                equals = true;
              }
            },
            *argBatchPtr);
        if(equals) {
          return Batch::WritablePtr(new ValueBatch(1, index));
        }
        ++index;
      }
      return Batch::WritablePtr(new ValueBatch(1, 0));
    }
  };
};

} // namespace boss::engines::bulk
