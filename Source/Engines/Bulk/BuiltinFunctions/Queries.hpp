#pragma once

#include "../OperatorUtils.hpp"

#include <map>
#include <vector>

namespace boss::engines::bulk {

template <typename BatchPrototypes> class Queries {
  using Utils = OperatorUtils<BatchPrototypes>;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    selection(prototypes);
    projection(prototypes);
    sorting(prototypes);
    grouping(prototypes);
  }

private:
  static void selection(BatchPrototypes& prototypes) {
    auto select = [](auto&& tableViewPtr, auto&& predicatePtr) -> Batch::WritablePtr {
      auto& tableOut = *tableViewPtr->template cloneAs<TableView>(true);

      auto forEachBatchOfRows = [&tableOut](CompoundBatch const& batch, auto const& toKeep) {
        // TODO: optimisation if toKeep is all true or false?
        // all true => just transfer the pointer (no copy)
        // all false => nothing to do

        auto batchOutPtr = WritableBatchPtr(batch.cloneAsCompoundBatch(true));
        Utils::insertRowValuesWithCondition(*batchOutPtr, batch, toKeep);
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

      tableViewPtr->visitChunks([&predicatePtr, &forEachBatchOfRows](auto&& batchOfRowsPtr) {
        // TODO: should evaluate only the columns used as criteria for the predicate
        // but for now it causes issues for where to set the "$tuple" information
        // (since the rows wouldn't be explicitely evaluated as a CBatch)
        // maybe should clean up "$tuple" if it is unused
        Batch::ReadablePtr evaluatedRowsPtr;
        bool evaluated = batchOfRowsPtr->evaluate(evaluatedRowsPtr);

        // evaluate the predicate
        std::vector<Batch::ReadablePtr> args;
        if(evaluated) {
          args.emplace_back(evaluatedRowsPtr);
        } else {
          args.emplace_back(batchOfRowsPtr);
        }
        auto toKeepPtr = predicatePtr->evaluateWith(args);

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
    };

    prototypes.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "Select", [select](auto&& tableViewPtr, auto&& predicatePtr) -> Batch::ReadablePtr {
          return select(tableViewPtr, predicatePtr);
        });
  }

  static void projection(BatchPrototypes& prototypes) {
    auto project = [](auto&& tableViewPtr, auto&& projectorPtr) -> Batch::WritablePtr {
      auto& tableOut = *(new TableView()); // not a clone so we clear columns too

      // evaluate the projection
      std::vector<Batch::ReadablePtr> args;
      args.emplace_back(tableViewPtr);
      auto projectionPtr = projectorPtr->evaluateWith(args);

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

      return Batch::WritablePtr(&tableOut);
    };

    prototypes.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "Project", [project](auto&& tableViewPtr, auto&& projectorPtr) {
          return project(tableViewPtr, projectorPtr);
        });
  }

  static void sorting(BatchPrototypes& prototypes) {
    // sortFunction: Function(tuple) return the key used for sorting
    // e.g to sort by first column: "Function"_(List_("tuple"_), "Column"_("tuple"_, 1))
    auto sortBy = [](auto&& tableViewPtr, auto&& sortFunctionPtr) -> Batch::WritablePtr {
      auto& tableOut = *tableViewPtr->template cloneAs<TableView>(true);

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
          Utils::insertRowValuesInOrder(*batchOutPtr, batch, rowIndices);
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

      tableViewPtr->visitChunks([&sortFunctionPtr, &forEachBatchOfRows](auto&& batchOfRowsPtr) {
        // evaluate the keys
        std::vector<Batch::ReadablePtr> args;
        args.emplace_back(batchOfRowsPtr);
        auto keysPtr = sortFunctionPtr->evaluateWith(args);

        // sort using these keys
        BatchPrototypes::BatchVisitDispatcher::visit(
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
    };

    prototypes.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "SortBy", [sortBy](auto&& tableViewPtr, auto&& sortFunctionPtr) -> Batch::ReadablePtr {
          return sortBy(tableViewPtr, sortFunctionPtr);
        });
  }

  static void grouping(BatchPrototypes& prototypes) {
    // groupFunction: Function(tuple) return a key
    // e.g to group by first column: "Function"_(List_("tuple"_), "Extract"_("tuple"_,
    // 1)) aggregator: Function("tuple", "aggregateResult") return the aggregate result
    // e.g to count: "Function"_(List_("tuple"_), "Count"_("Extract"_("tuple"_, 1)))
    // e.g to sum: "Function"_(List_("tuple"_), "Sum_("Extract"_("tuple"_, 1)))
    // e.g to return the key: "Function"_(List_("tuple"_), "Extract"_("tuple"_, 1))
    auto group = [](auto&& tableViewPtr, auto&& groupFunctionPtr,
                    auto const& aggregator) -> Batch::WritablePtr {
      auto& tableOut = *(new TableView()); // not a clone so we clear columns too

      auto aggregate = [&aggregator](auto& destbatches, auto const& srcBatch, auto const& sorted) {
        auto groupedPtr = WritableBatchPtr(srcBatch.cloneAsCompoundBatch(true));
        for(auto const& sortedIt : sorted) {
          // prepare the rows for the group to be processed
          auto const& rowIndices = sortedIt.second;
          Utils::insertRowValuesInOrder(*groupedPtr, srcBatch, rowIndices);

          // process to be called for each group of (sorted) table rows
          std::vector<Batch::ReadablePtr> args;
          args.emplace_back(groupedPtr);
          auto aggregatedBatchPtr = aggregator.evaluateWith(args);
          destbatches.emplace_back(std::move(aggregatedBatchPtr));
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

      tableViewPtr->visitChunks([&groupFunctionPtr, &forEachBatchOfRows](auto&& batchOfRowsPtr) {
        // evaluate the keys
        std::vector<Batch::ReadablePtr> args;
        args.emplace_back(batchOfRowsPtr);
        auto keysPtr = groupFunctionPtr->evaluateWith(args);

        // sort using these keys
        BatchPrototypes::BatchVisitDispatcher::visit(
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
    };

    prototypes
        .template argBatchTypes<TableView, FunctionBatch,
                                AllowedBatches<FunctionBatch, SymbolBatch>>()
        .template registerFunction<3>(
            "Group",
            [&prototypes, group](auto&& tableViewPtr, auto&& groupFunctionPtr,
                                 auto&& aggregatorPtr) -> Batch::ReadablePtr {
              Batch::WritablePtr resultPtr;
              BatchVisitDispatcher<FunctionBatch, SymbolBatch>::visit(
                  [&prototypes, &group, &tableViewPtr, &groupFunctionPtr,
                   &resultPtr](auto const& aggregatorBatch) {
                    using BatchType = std::decay_t<decltype(aggregatorBatch)>;
                    if constexpr(std::is_same_v<BatchType, FunctionBatch>) {
                      resultPtr = group(tableViewPtr, groupFunctionPtr, aggregatorBatch);

                    } else {
                      // construct an expression batch from the head (assuming single symbol
                      // value) also assuming a function with 1 argument only
                      Symbol const& head = *aggregatorBatch.begin();
                      Batch::WritablePtr bodyBatchPtr(prototypes.createBatch(head, 1));
                      // we pass a symbol as unique argument
                      Symbol functionArg("tuple");
                      bodyBatchPtr->append(ComplexExpression(head, {functionArg}));
                      // and now we create a function batch using this expression as body
                      WritableBatchPtr<FunctionBatch> functionPtr(new FunctionBatch(
                          FunctionBatch::ParameterList{functionArg}, std::move(bodyBatchPtr)));
                      resultPtr = group(tableViewPtr, groupFunctionPtr, *functionPtr);
                    }
                  },
                  *aggregatorPtr);
              return resultPtr;
            });
  }
};

} // namespace boss::engines::bulk
