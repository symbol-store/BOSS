#pragma once

#include "../OperatorUtils.hpp"

namespace boss::engines::bulk {

template <typename BatchPrototypes> class Collections {
  using Utils = OperatorUtils<BatchPrototypes>;
  using AnySimpleBatch = typename BatchPrototypes::AnySimpleBatch;
  using NonSymbolicBatch = typename BatchPrototypes::NonSymbolicBatch;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    prototypes.template argBatchTypes<NonSymbolicBatch, ValueBatch<int>>()
        .template registerFunction<2>(
            "Extract", [](auto&& exprBatchesPtr, auto&& indexBatchPtr) -> Batch::ReadablePtr {
              auto extraction = [](auto const& exprBatch, size_t index) {
                using BatchType = std::decay_t<decltype(exprBatch)>;
                using ValueType = typename BatchType::ValueType;
                if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
                  return exprBatch.extract(index);
                } else {
                  auto const& value = static_cast<ValueType>(*(exprBatch.begin() + index));
                  return Batch::WritablePtr(Engine::getBatchFactory().createBatch(value));
                }
              };

              auto const& exprBatches = *exprBatchesPtr;
              auto const& indexBatch = *indexBatchPtr;
              if(indexBatch.size() == 1) {
                // special case for single batch extraction
                size_t index = *indexBatch.begin() - 1u;
                return Batch::ReadablePtr(extraction(exprBatches, index));
              }
              // general case for multiple extraction
              // exprBatchesPtr should be a compound
              using BatchType = std::decay_t<decltype(exprBatches)>;
              auto compoundBatchPtr =
                  WritableBatchPtr(exprBatches.template cloneAs<BatchType>(true));
              if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
                auto& compoundBatch = *compoundBatchPtr;
                auto indexIt = indexBatch.begin();
                auto exprIt = exprBatches.begin();
                std::vector<Batch::ReadablePtr> argBatches;
                argBatches.reserve(indexBatch.size());
                for(; indexIt != indexBatch.end() && exprIt != exprBatches.end();
                    ++indexIt, ++exprIt) {
                  size_t index = *indexIt - 1u;
                  auto exprBatchPtr = *exprIt;
                  BatchPrototypes::BatchVisitDispatcher::visit(
                      [&index, &argBatches, &extraction](auto const& exprBatch) {
                        argBatches.emplace_back(extraction(exprBatch, index));
                      },
                      *exprBatchPtr);
                }
                compoundBatch.append(argBatches);
              }
              return Batch::ReadablePtr(std::move(compoundBatchPtr));
            });

    prototypes.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "First", [](auto&& batchPtrExpr) {
          using BatchPtrType = std::decay_t<decltype(batchPtrExpr)>;
          using BatchType = typename BatchPtrType::BatchType;
          using ValueType = typename BatchType::ValueType;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            return batchPtrExpr->extract(0);
          } else {
            auto const& value = static_cast<ValueType>(*batchPtrExpr->begin());
            return Batch::WritablePtr(Engine::getBatchFactory().createBatch(value));
          }
        });

    prototypes.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Last", [](auto&& batchPtrExpr) {
          using BatchPtrType = std::decay_t<decltype(batchPtrExpr)>;
          using BatchType = typename BatchPtrType::BatchType;
          using ValueType = typename BatchType::ValueType;
          size_t index = batchPtrExpr->size() - 1;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            return batchPtrExpr->extract(index);
          } else {
            auto const& value = static_cast<ValueType>(*(batchPtrExpr->begin() + index));
            return Batch::WritablePtr(Engine::getBatchFactory().createBatch(value));
          }
        });

    prototypes.template argBatchTypes<AllowedBatches<CompoundBatch, TableView>, ValueBatch<int>>()
        .template registerFunction<2>("Column", [](auto&& batchPtrExpr, auto&& batchPtrNth) {
          size_t index = *batchPtrNth->begin() - 1;
          return batchPtrExpr->column(index);
        });

    prototypes.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Length", [](auto&& batchPtrExpr) {
          int value = batchPtrExpr->size();
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(value));
        });

    prototypes.template argBatchTypes<CompoundBatch, AnySimpleBatch>().template registerFunction<2>(
        "IndexOf", [](auto&& listBatchPtr, auto&& valueBatchPtr) {
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
              return Batch::WritablePtr(Engine::getBatchFactory().createBatch(index));
            }
            ++index;
          }
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(0));
        });
  }
};

} // namespace boss::engines::bulk
