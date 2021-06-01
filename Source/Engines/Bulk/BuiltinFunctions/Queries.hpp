#pragma once

#include "../Batch/CompoundBatch.hpp"
#include "../Executor.hpp"
#include "../Operator.hpp"

#include <map>
#include <vector>

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Queries {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<SelectOperator>("Select");
    operatorRegistry.template registerOperator<ProjectOperator>("Project");
    operatorRegistry.template registerOperator<SortByOperator>("SortBy");
    operatorRegistry.template registerOperator<GroupOperator>("Group");
  }

private:
  class SelectOperator
      : public Operator<2, AllowedBatches<CompoundBatch>, AllowedBatches<CompoundBatch>> {
  public:
    template <typename TableType, typename PredicateType>
    auto evaluate(TableType&& tableBatchPtr, PredicateType&& predicatePtr) const {
      auto& tableOut = *tableBatchPtr->cloneAsCompoundBatch(true);

      auto forEachBatchOfRows = [&tableOut](CompoundBatch const& batch, auto const& toKeep) {
        // TODO: optimisation if toKeep is all true or false?
        // all true => just transfer the pointer (no copy)
        // all false => nothing to do

        auto batchOutPtr = WritableBatchPtr(batch.cloneAsCompoundBatch(true));
        OperatorUtils::insertRowValuesWithCondition(*batchOutPtr, batch, toKeep);
        size_t numColumns = batchOutPtr->numArguments();
        if(numColumns > 0) {
          std::vector<Batch::ReadablePtr> argBatches;
          argBatches.reserve(numColumns);
          for(auto batchPtr : *batchOutPtr) {
            argBatches.emplace_back(std::move(batchPtr));
          }
          tableOut.append(std::move(argBatches));
        }
      };

      tableBatchPtr->visitChunks([&predicatePtr, &forEachBatchOfRows](auto&& batchOfRowsPtr) {
        // TODO: should evaluate only the columns used as criteria for the predicate
        // but for now it causes issues for where to set the "$tuple" information
        // (since the rows wouldn't be explicitely evaluated as a CBatch)
        // maybe should clean up "$tuple" if it is unused
        Batch::ReadablePtr evaluatedRowsPtr;
        bool evaluated = Executor::evaluate(*batchOfRowsPtr, evaluatedRowsPtr);

        // evaluate the predicate
        std::vector<Batch::ReadablePtr> args;
        if(evaluated) {
          args.emplace_back(evaluatedRowsPtr);
        } else {
          args.emplace_back(batchOfRowsPtr);
        }
        Batch::ReadablePtr toKeepPtr;
        if(!Executor::evaluate(*predicatePtr, args, toKeepPtr)) {
          return;
        }

        // apply the predicate
        BatchVisitDispatcher<ValueBatch<bool>>::visit(
            [&evaluated, &batchOfRowsPtr, &evaluatedRowsPtr,
             &forEachBatchOfRows](auto const& toKeep) {
              if(evaluated) {
                BatchVisitDispatcher<CompoundBatch>::visit(
                    [&toKeep, &forEachBatchOfRows](auto const& batchofRows) {
                      forEachBatchOfRows(batchofRows, toKeep);
                    },
                    *evaluatedRowsPtr);
              } else {
                forEachBatchOfRows(*batchOfRowsPtr, toKeep);
              }
            },
            *toKeepPtr);
      });

      return Batch::WritablePtr(&tableOut);
    }
  };

  class ProjectOperator
      : public Operator<2, AllowedBatches<CompoundBatch>, AllowedBatches<CompoundBatch>> {
  public:
    template <typename TableType, typename ProjectorType>
    auto evaluate(TableType&& tableBatchPtr, ProjectorType&& projectorPtr) const {
      auto& tableOut = *(new CompoundBatch(true)); // not a clone so we clear columns too

      // evaluate the projection
      std::vector<Batch::ReadablePtr> args;
      args.emplace_back(tableBatchPtr);
      Batch::ReadablePtr projectionPtr;
      bool evaluated = Executor::evaluate(*projectorPtr, args, projectionPtr);

      if(evaluated) {
        // copy the new batches back to the table
        BatchVisitDispatcher<CompoundBatch>::visit(
            [&tableOut](auto const& projectionBatch) {
              std::vector<Batch::ReadablePtr> columnBatches;
              for(auto srcBatchPtr : projectionBatch) {
                columnBatches.emplace_back(std::move(srcBatchPtr));
              }
              tableOut.append(columnBatches);
            },
            *projectionPtr);
      }

      return Batch::WritablePtr(&tableOut);
    }
  };

  // sortFunction: Function(tuple) return the key used for sorting
  // e.g to sort by first column: "Function"_(List_("tuple"_), "Column"_("tuple"_, 1))
  class SortByOperator
      : public Operator<2, AllowedBatches<CompoundBatch>, AllowedBatches<CompoundBatch>> {
  public:
    template <typename TableType, typename SortFunctionType>
    auto evaluate(TableType&& tableBatchPtr, SortFunctionType&& sortFunctionPtr) const {
      auto& tableOut = *tableBatchPtr->cloneAsCompoundBatch(true);

      auto forEachBatchOfRows = [&tableOut](CompoundBatch const& batch, auto& keys) {
        using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

        size_t batchSize = batch.size();
        if(batchSize == 0) {
          return;
        }

        // create sorted indexes
        // TODO: any way to reserve?
        auto keyIt = keys.begin();
        using SortMap =
            std::conditional_t<std::is_same_v<ElementType, Symbol>,
                               std::map<Symbol, std::vector<size_t>, CompareSymbolNames>,
                               std::map<ElementType, std::vector<size_t>>>;
        SortMap sorted;
        for(size_t rowIndex = 0; rowIndex < batchSize; ++rowIndex, ++keyIt) {
          sorted[*keyIt].push_back(rowIndex);
        }

        auto batchOutPtr = WritableBatchPtr(batch.cloneAsCompoundBatch(true));
        for(auto const& sortedIt : sorted) {
          auto const& rowIndices = sortedIt.second;
          OperatorUtils::insertRowValuesInOrder(*batchOutPtr, batch, rowIndices);
        }

        size_t numColumns = batchOutPtr->numArguments();
        if(numColumns > 0) {
          std::vector<Batch::ReadablePtr> argBatches;
          argBatches.reserve(numColumns);
          for(auto batchPtr : *batchOutPtr) {
            argBatches.emplace_back(std::move(batchPtr));
          }
          tableOut.append(std::move(argBatches));
        }
      };

      tableBatchPtr->visitChunks([&sortFunctionPtr, &forEachBatchOfRows](auto&& batchOfRowsPtr) {
        // evaluate the keys
        std::vector<Batch::ReadablePtr> args;
        args.emplace_back(batchOfRowsPtr);
        Batch::ReadablePtr keysPtr;
        if(!Executor::evaluate(*sortFunctionPtr, args, keysPtr)) {
          return;
        }

        // sort using these keys
        OperatorUtils::AnyBatchVisitDispatcher::visit(
            [&batchOfRowsPtr, &forEachBatchOfRows](auto const& keys) {
              if(keys.size() == 0) {
                return;
              }
              using KeyBatchType = std::decay_t<decltype(keys)>;
              if constexpr(!std::is_base_of_v<CompoundBatch, KeyBatchType>) {
                forEachBatchOfRows(*batchOfRowsPtr, keys);
              } else {
                // [https://github.com/symbol-store/BOSS/issues/86]
                // how to do sorting if we handle list/expression as a key?
                // create a tuple of the values?
              }
            },
            *keysPtr);
      });

      return Batch::WritablePtr(&tableOut);
    }
  };

  // groupFunction: Function(tuple) return a key
  // e.g to group by first column: "Function"_("tuple"_, "Extract"_("tuple"_, 1))
  // aggregator: Function("tuple", "aggregateResult") return the aggregate result
  // e.g to count: "Function"_("tuple"_, "Count"_("Column"_("tuple"_, 1)))
  // e.g to sum: "Function"_("tuple"_, "Sum_("Column"_("tuple"_, 1)))
  // e.g to return the key: "Function"_("tuple"_, "Column"_("tuple"_, 1))
  class GroupOperator
      : public Operator<3, AllowedBatches<CompoundBatch>, AllowedBatches<CompoundBatch>,
                        AllowedBatches<CompoundBatch, SymbolBatch>> {
  public:
    template <typename TableType, typename GroupFunctionType, typename AggregatorType>
    auto evaluate(TableType&& tableBatchPtr, GroupFunctionType&& groupFunctionPtr,
                  AggregatorType&& aggregatorPtr) const {
      Batch::WritablePtr resultPtr;
      BatchVisitDispatcher<CompoundBatch, SymbolBatch>::visit(
          [&tableBatchPtr, &groupFunctionPtr, &resultPtr](auto const& aggregatorBatch) {
            using BatchType = std::decay_t<decltype(aggregatorBatch)>;
            if constexpr(std::is_same_v<BatchType, CompoundBatch>) {
              resultPtr = group(tableBatchPtr, groupFunctionPtr, aggregatorBatch);
            } else {
              // special case when providing aggregator as just a symbol head

              // construct an expression batch from the head (assuming single symbol
              // value) also assuming a function with 1 argument only
              Symbol const& head = *aggregatorBatch.begin();
              auto* bodyBatch = new CompoundBatch(head);
              // we pass a symbol as unique argument
              Symbol parameter("tuple");
              bodyBatch->append(ComplexExpression(head, {parameter}));

              // construct the parameters batch
              auto* paramsBatch = new SymbolBatch(1, parameter);

              // and now we create a function batch using the parameters + body
              CompoundBatch functionBatch(Symbol("Function"));
              functionBatch.append({Batch::ReadablePtr((Batch const*)(paramsBatch)),
                                    Batch::ReadablePtr((Batch const*)(bodyBatch))});
              resultPtr = group(tableBatchPtr, groupFunctionPtr, functionBatch);
            }
          },
          *aggregatorPtr);
      return resultPtr;
    }

  private:
    template <typename TableType, typename GroupFunctionType, typename AggregatorType>
    static auto group(TableType&& tableBatchPtr, GroupFunctionType&& groupFunctionPtr,
                      AggregatorType&& aggregator) {
      auto& tableOut = *(new CompoundBatch(true)); // not a clone so we clear columns too

      // to be called for each group of (sorted) table rows
      auto aggregate = [&aggregator](auto& destbatches, auto const& srcBatch, auto const& sorted) {
        auto groupedPtr = WritableBatchPtr(srcBatch.cloneAsCompoundBatch(true));

        for(auto const& sortedIt : sorted) {
          // prepare the rows for the group to be processed
          auto const& rowIndices = sortedIt.second;
          OperatorUtils::insertRowValuesInOrder(*groupedPtr, srcBatch, rowIndices);

          // evaluate the aggregate on the group
          std::vector<Batch::ReadablePtr> args;
          args.emplace_back(groupedPtr);
          Batch::ReadablePtr aggregatedBatchPtr;
          if(Executor::evaluate(aggregator, args, aggregatedBatchPtr)) {
            destbatches.emplace_back(std::move(aggregatedBatchPtr));
          }

          groupedPtr->clear();
        }
      };

      auto forEachBatchOfRows = [&tableOut, &aggregate](CompoundBatch const& batch,
                                                        auto const& keys) {
        using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

        // create sorted indexes
        // TODO: any way to reserve?
        using SortMap =
            std::conditional_t<std::is_same_v<ElementType, Symbol>,
                               std::map<Symbol, std::vector<size_t>, CompareSymbolNames>,
                               std::map<ElementType, std::vector<size_t>>>;
        SortMap sorted;
        size_t batchSize = batch.size();
        auto keyIt = keys.begin();
        for(size_t rowIndex = 0; rowIndex < batchSize; ++rowIndex, ++keyIt) {
          sorted[*keyIt].push_back(rowIndex);
        }

        std::vector<Batch::WritablePtr> newBatches;
        aggregate(newBatches, batch, sorted);

        tableOut.append(std::vector<Batch::ReadablePtr>(newBatches.begin(), newBatches.end()));
      };

      tableBatchPtr->visitChunks([&groupFunctionPtr, &forEachBatchOfRows](auto&& batchOfRowsPtr) {
        // evaluate the keys
        std::vector<Batch::ReadablePtr> args;
        args.emplace_back(batchOfRowsPtr);
        Batch::ReadablePtr keysPtr;
        if(!Executor::evaluate(*groupFunctionPtr, args, keysPtr)) {
          return;
        }

        // sort using these keys
        OperatorUtils::AnyBatchVisitDispatcher::visit(
            [&batchOfRowsPtr, &forEachBatchOfRows](auto const& keys) {
              if(keys.size() == 0) {
                return;
              }
              using KeyBatchType = std::decay_t<decltype(keys)>;
              if constexpr(!std::is_base_of_v<CompoundBatch, KeyBatchType>) {
                forEachBatchOfRows(*batchOfRowsPtr, keys);
              } else {
                // [https://github.com/symbol-store/BOSS/issues/86]
                // how to do grouping if we handle list/expression as a key?
                // create a tuple of the values?
              }
            },
            *keysPtr);
      });

      return Batch::WritablePtr(&tableOut);
    }
  };
};

} // namespace boss::engines::bulk
