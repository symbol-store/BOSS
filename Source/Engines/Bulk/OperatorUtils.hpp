#pragma once

#include "Batch/Batch.hpp"
#include "BatchVisitDispatcher.hpp"

namespace boss::engines::bulk {

/** A set of util functions which can be called by the operators to avoid repeating common code.*/
template <typename... SupportedTypes> class OperatorUtils {
public:
  using AnyBatchVisitDispatcher =
      BatchVisitDispatcher<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch,
                           CompoundBatch>;

  using AnyBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch, CompoundBatch>;

  using NonSymbolicBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, CompoundBatch>;

  using AnySimpleBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch>;

  /// to iterate and evaluate on each element of a batch
  template <typename Func, typename... BatchPtrIn>
  static Batch::WritablePtr evaluateElements(Func&& func, BatchPtrIn&&... in) {
    auto apply = [&](auto& out, auto&&... inIt) {
      auto outIt = out.begin();
      for(; outIt != out.end(); ++outIt, ((++inIt), ...)) {
        *outIt = func((*inIt)...);
      }
    };

    using ReturnType = ReturnType<std::decay_t<decltype(func)>, std::decay_t<decltype(in)>...>;
    if constexpr(std::is_same_v<ReturnType, Symbol>) {
      // assuming symbol to be always a single output
      // (different symbols must be dispatched to different batches!)
      auto* outputBatch = new SymbolBatch(1);
      apply(*outputBatch, in->begin()...);
      return Batch::WritablePtr(outputBatch);
    } else if constexpr(std::is_same_v<ReturnType, ComplexExpression>) {
      auto* outputBatch = new CompoundBatch();
      apply(*outputBatch, in->begin()...);
      return Batch::WritablePtr(outputBatch);
    } else {
      size_t outputSize = 1;
      (..., [&outputSize, &in]() { outputSize = std::max(outputSize, in->size()); }());
      auto* outputBatch = new ValueBatch<ReturnType>();
      outputBatch->resize(outputSize);
      apply(*outputBatch, in->begin()...);
      return Batch::WritablePtr(outputBatch);
    }
  }

  /// copy row values in sorted order (based on indices), column per column
  template <typename DestBatchType, typename SrcBatchType>
  static void insertRowValuesInOrder(DestBatchType& destBatch, SrcBatchType const& srcBatch,
                                     std::vector<size_t> const& rowIndices) {
    static_assert(std::is_base_of_v<std::remove_const_t<SrcBatchType>, DestBatchType>);
    // special case for columns of complex expressions
    if constexpr(std::is_base_of_v<CompoundBatch, SrcBatchType>) {
      // check if the columns already exist in the destination
      // if not initialise the right arg batch types (but empty so far)
      if(destBatch.numArguments() == 0) {
        std::vector<Batch::ReadablePtr> srcArgBatches;
        srcArgBatches.reserve(srcBatch.numArguments());
        for(auto const& srcArgBatchPtr : srcBatch) {
          srcArgBatches.emplace_back(srcArgBatchPtr);
        }
        destBatch.initArguments(srcArgBatches);
      }
      // then recursive call for every argument
      size_t childrenSize = 0;
      auto destArgBatchIt = destBatch.begin();
      srcBatch.template visitBatches<AnyBatchVisitDispatcher>(
          [&childrenSize, &destArgBatchIt, &rowIndices](auto const& srcColumn) {
            using ColumnBatchType = std::decay_t<decltype(srcColumn)>;
            // insert to existing arg column
            auto destArgBatchPtr = *destArgBatchIt;
            BatchVisitDispatcher<ColumnBatchType>::visit(
                [&childrenSize, &srcColumn, &rowIndices](auto& destColumn) {
                  insertRowValuesInOrder(destColumn, srcColumn, rowIndices);
                  childrenSize = destColumn.size();
                },
                *Batch::WritablePtr::asWritable(std::move(destArgBatchPtr)));
            ++destArgBatchIt;
          });
      // and make sure to adjust the size of the parent array
      destBatch.resize(childrenSize);
    } else {
      size_t previousnumRows = destBatch.size();
      destBatch.resize(previousnumRows + rowIndices.size());
      auto destBatchIt = destBatch.begin() + previousnumRows;
      for(auto rowIndexIt = rowIndices.begin(); rowIndexIt != rowIndices.end();
          ++rowIndexIt, ++destBatchIt) {
        auto srcBatchIt = srcBatch.begin() + *rowIndexIt;
        *destBatchIt = *srcBatchIt;
      }
    }
  }

  /// copy row values if matches a condition, column per column
  template <typename DestBatchType, typename SrcBatchType, typename ConditionBatchType>
  static void insertRowValuesWithCondition(DestBatchType& destBatch, SrcBatchType const& srcBatch,
                                           ConditionBatchType const& conditionBatch) {
    static_assert(std::is_base_of_v<std::remove_const_t<SrcBatchType>, DestBatchType>);
    if constexpr(std::is_base_of_v<CompoundBatch, SrcBatchType>) {
      // special case for columns of complex expressions
      std::vector<Batch::WritablePtr> argBatches;
      argBatches.reserve(srcBatch.numArguments());
      // check if the columns already exist in the destination
      // assuming the destination has always the same number of args!
      for(auto destArgBatchPtr : destBatch) {
        argBatches.emplace_back(std::move(destArgBatchPtr));
      }
      // then recursive call for every argument
      auto destArgBatchIt = argBatches.begin();
      auto destArgBatchEnd = argBatches.end();
      srcBatch.template visitBatches<AnyBatchVisitDispatcher>(
          [&destArgBatchIt, &destArgBatchEnd, &conditionBatch, &argBatches](auto const& srcColumn) {
            using ColumnBatchType = std::decay_t<decltype(srcColumn)>;
            if(destArgBatchIt != destArgBatchEnd) {
              // insert to existing arg column
              auto& destArgBatchPtr = *destArgBatchIt;
              BatchVisitDispatcher<ColumnBatchType>::visit(
                  [&srcColumn, &conditionBatch](auto& destColumn) {
                    insertRowValuesWithCondition(destColumn, srcColumn, conditionBatch);
                  },
                  *destArgBatchPtr);
              ++destArgBatchIt;
              return;
            }
            // create new arg column
            auto newColumnBatchPtr =
                WritableBatchPtr(srcColumn.template cloneAs<ColumnBatchType>(true));
            insertRowValuesWithCondition(*newColumnBatchPtr, srcColumn, conditionBatch);
            Batch::ReadablePtr toInsertPtr(std::move(newColumnBatchPtr));
            argBatches.emplace_back(std::move(toInsertPtr));
          });
      // if they are new arg columns, insert them now
      if(destBatch.numArguments() == 0) {
        if(!argBatches.empty() && argBatches[0]->size() > 0) {
          destBatch.append(std::vector<Batch::ReadablePtr>(argBatches.begin(), argBatches.end()));
        }
      }
    } else {
      size_t numRows = destBatch.size();
      destBatch.resize(numRows + srcBatch.size()); // pessimistic
      auto srcBatchIt = srcBatch.begin();
      auto conditionIt = conditionBatch.begin();
      auto destBatchIt = destBatch.begin() + numRows;
      for(; srcBatchIt != srcBatch.end(); ++srcBatchIt, ++conditionIt) {
        if(!*conditionIt) {
          continue;
        }
        *destBatchIt = *srcBatchIt;
        ++destBatchIt;
        ++numRows;
      }
      destBatch.resize(numRows); // shrink it back
    }
  }

private:
  // to retrieve return type for a specific set of Batch argument types
  template <typename T> using FromBatchTypeToElementType = typename T::ValueType;
  template <typename Func, typename... BatchPtrTypes>
  using ReturnType = typename std::invoke_result_t<
      Func, FromBatchTypeToElementType<typename BatchPtrTypes::BatchType>...>;
};

} // namespace boss::engines::bulk
