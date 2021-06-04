#pragma once

#include "../ArrowExtensions/CompoundArray.hpp"
#include "../BatchVisitDispatcher.hpp"
#include "../Executor.hpp"
#include "../Operator.hpp"

#include <map>
#include <vector>

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Queries {
  using TableArgument = typename OperatorUtils::TableArgument;

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
      : public Operator<2, TableArgument, AllowedArguments<BulkComplexExpression>> {
  public:
    template <typename TableType, typename PredicateType>
    BulkExpression evaluate(TableType const& tableArrayPtr, PredicateType const& predicate) const {
      // copy with 'clear' flag, so we keep the column builders
      auto tableOutPtr = std::make_shared<CompoundArray>(*tableArrayPtr, true);
      auto& tableOut = *tableOutPtr;

      tableArrayPtr->visitPartitions([&tableOut, &predicate](auto&& batchOfRowsPtr) {
        // TODO: should evaluate only the columns used as criteria for the predicate
        // but for now it causes issues for where to set the "$tuple" information
        // (since the rows wouldn't be explicitely evaluated as a CBatch)
        // maybe should clean up "$tuple" if it is unused
        BulkExpression evaluatedRows;
        bool evaluated = Executor::evaluate(batchOfRowsPtr, evaluatedRows);
        auto* pointerToArrayPtr =
            std::get_if<std::shared_ptr<CompoundArray>>((BulkExpression::SuperType*)&evaluatedRows);
        auto const& batchOfRows =
            evaluated && pointerToArrayPtr != nullptr ? **pointerToArrayPtr : *batchOfRowsPtr;

        BulkExpressionArguments inputs;
        if(evaluated) {
          inputs.emplace_back(evaluatedRows);
        } else {
          inputs.emplace_back(batchOfRowsPtr);
        }

        // evaluate the predicate
        BulkExpression toKeepOutput;
        if(!Executor::evaluate(predicate, inputs, toKeepOutput)) {
          return;
        }

        // apply the predicate
        BatchVisitDispatcher<bool, std::shared_ptr<ValueArray<bool>>>::visit(
            [&tableOut, &evaluated, &evaluatedRows, &batchOfRowsPtr](auto const& toKeep) {
              if(evaluated) {
                auto* evaluatedArrayPtr =
                    std::get_if<std::shared_ptr<CompoundArray>>(&evaluatedRows);
                if(evaluatedArrayPtr) {
                  SelectOperator::select(tableOut, **evaluatedArrayPtr, toKeep);
                }
              } else {
                SelectOperator::select(tableOut, *batchOfRowsPtr, toKeep);
              }
            },
            toKeepOutput);
      });

      return tableOutPtr;
    }

  private:
    static void select(CompoundArray& tableOut, CompoundArray const& srcBatchArray,
                       std::shared_ptr<ValueArray<bool>> toKeepPtr) {
      CompoundArray destBatchArray(true);
      OperatorUtils::insertRowValuesWithCondition(destBatchArray, srcBatchArray, *toKeepPtr);
      if(destBatchArray.numArguments() > 0) {
        tableOut.append(std::move(destBatchArray));
      }
    }

    static void select(CompoundArray& tableOut, CompoundArray const& srcBatchArray, bool toKeep) {
      // special case when toKeep is just a boolean
      // we can just do one single check and take all or nothing
      if(toKeep) {
        CompoundArray destBatchArray(true);
        OperatorUtils::insertAllRows(destBatchArray, srcBatchArray);
        if(destBatchArray.numArguments() > 0) {
          tableOut.append(std::move(destBatchArray));
        }
      }
    }
  };

  // projector: Function(tuple) return the list of columns we want to keep
  // e.g to project on the 1st column: "Function"_("tuple"_, "List"_("Column"_("tuple"_, 1)))
  class ProjectOperator
      : public Operator<2, TableArgument, AllowedArguments<BulkComplexExpression>> {
  public:
    template <typename TableType, typename ProjectorType>
    BulkExpression evaluate(TableType const& tableArrayPtr, ProjectorType const& projector) const {
      // not a copy so we clear the column builders too
      auto tableOutPtr = std::make_shared<CompoundArray>(true);
      auto& tableOut = *tableOutPtr;

      // evaluate the projection
      BulkExpressionArguments inputs{tableArrayPtr};
      BulkExpression projectedColumns;
      bool evaluated = Executor::evaluate(projector, inputs, projectedColumns);

      if(evaluated) {
        BatchVisitDispatcher<std::shared_ptr<CompoundArray>, BulkComplexExpression>::visit(
            [&tableOut](auto const& columns) { ProjectOperator::project(tableOut, columns); },
            projectedColumns);
      }

      return tableOutPtr;
    }

  private:
    static void project(CompoundArray& tableOut, std::shared_ptr<CompoundArray> const& columnArrayPtr) {
      CompoundArray projectedArray(*columnArrayPtr, false);
      tableOut.append(std::move(projectedArray));
    }

    static void project(CompoundArray& tableOut, BulkComplexExpression const& columnList) {
      std::vector<BatchData> argData;
      argData.reserve(columnList.getArguments().size());
      for(auto column : columnList.getArguments()) {
        OperatorUtils::CollectionVisitDispatcher::visit(
            [&argData](auto const& srcColumnPtr) { argData.emplace_back(srcColumnPtr->data()); },
            column);
      }
      tableOut.append(columnList.getHead(), argData);
    }
  };

  // sortFunction: Function(tuple) return the key used for sorting
  // e.g to sort by first column: "Function"_(List_("tuple"_), "Column"_("tuple"_, 1))
  class SortByOperator
      : public Operator<2, TableArgument, AllowedArguments<BulkComplexExpression>> {
  public:
    template <typename TableType, typename SortFunctionType>
    BulkExpression evaluate(TableType const& tableArrayPtr,
                            SortFunctionType const& sortFunction) const {
      // copy with 'clear' flag, so we keep the column builders
      auto tableOutPtr = std::make_shared<CompoundArray>(*tableArrayPtr, true);
      auto& tableOut = *tableOutPtr;

      tableArrayPtr->visitPartitions([&sortFunction, &tableOut](auto const& batchOfRowsPtr) {
        // evaluate the keys
        BulkExpressionArguments inputs{batchOfRowsPtr};
        BulkExpression keysOutput;
        if(!Executor::evaluate(sortFunction, inputs, keysOutput)) {
          return;
        }

        // sort using these keys
        OperatorUtils::AnyTypeVisitDispatcher::visit(
            [&batchOfRowsPtr, &tableOut](auto const& keys) {
              SortByOperator::sort(tableOut, *batchOfRowsPtr, keys);
            },
            keysOutput);
      });

      return tableOutPtr;
    }

  private:
    template <typename ElementType>
    static void sort(CompoundArray& tableOut, CompoundArray const& srcBatchArray,
                     ElementType const& /*key*/) {
      // special case with only a unique key...
      // just copy all the rows
      CompoundArray destBatchArray(srcBatchArray, true);
      OperatorUtils::insertAllRows(destBatchArray, srcBatchArray);
      if(destBatchArray.numArguments() > 0) {
        tableOut.append(std::move(destBatchArray));
      }
    }

    template <typename ElementType>
    static void sort(CompoundArray& tableOut, CompoundArray const& srcBatchArray,
                     std::shared_ptr<ValueArray<ElementType>> const& keysPtr) {
      size_t batchSize = srcBatchArray.length();
      if(batchSize == 0) {
        return;
      }

      // create sorted indexes
      // TODO: any way to reserve?
      using SortMap = std::conditional_t<std::is_same_v<ElementType, Symbol>,
                                         std::map<Symbol, std::vector<size_t>, CompareSymbolNames>,
                                         std::map<ElementType, std::vector<size_t>>>;
      SortMap sorted;
      auto keyIt = keysPtr->begin();
      for(size_t rowIndex = 0; rowIndex < batchSize; ++rowIndex, ++keyIt) {
        sorted[(ElementType)*keyIt].push_back(rowIndex);
      }

      CompoundArray destBatchArray(true);
      for(auto const& sortedIt : sorted) {
        auto const& rowIndices = sortedIt.second;
        OperatorUtils::insertRowValuesInOrder(destBatchArray, srcBatchArray, rowIndices);
      }

      if(destBatchArray.numArguments() > 0) {
        tableOut.append(std::move(destBatchArray));
      }
    }

    static void sort(CompoundArray& tableOut, CompoundArray const& srcBatchArray,
                     std::shared_ptr<CompoundArray> const& keysPtr) {
      // [https://github.com/symbol-store/BOSS/issues/86]
      // how to do sorting if we handle list/expression as a key?
      // create a tuple of the values?
    }
  };

  // groupFunction: Function(tuple) return a key
  // e.g to group by first column: "Function"_("tuple"_, "Extract"_("tuple"_, 1))
  // aggregator: Function("tuple", "aggregateResult") return the aggregate result
  // e.g to count: "Function"_("tuple"_, "Count"_("Column"_("tuple"_, 1)))
  // e.g to sum: "Function"_("tuple"_, "Sum_("Column"_("tuple"_, 1)))
  // e.g to return the key: "Function"_("tuple"_, "Column"_("tuple"_, 1))
  class GroupOperator : public Operator<3, TableArgument, AllowedArguments<BulkComplexExpression>,
                                        AllowedArguments<Symbol, BulkComplexExpression>> {
  public:
    template <typename TableType, typename GroupFunctionType, typename AggregatorType>
    BulkExpression evaluate(TableType const& tableArrayPtr, GroupFunctionType const& groupFunction,
                            AggregatorType const& aggregatorFunction) const {
      // not a copy so we clear the column builders too
      auto tableOutPtr = std::make_shared<CompoundArray>(true);
      auto& tableOut = *tableOutPtr;

      tableArrayPtr->visitPartitions(
          [&groupFunction, &aggregatorFunction, &tableOut](auto const& batchOfRowsPtr) {
            // evaluate the keys
            BulkExpressionArguments inputs{batchOfRowsPtr};
            BulkExpression keysOutput;
            if(!Executor::evaluate(groupFunction, inputs, keysOutput)) {
              return;
            }

            // group using these keys and aggregate
            OperatorUtils::AnyTypeVisitDispatcher::visit(
                [&batchOfRowsPtr, &aggregatorFunction, &tableOut](auto const& keys) {
                  group(tableOut, batchOfRowsPtr, keys, aggregatorFunction);
                },
                keysOutput);
          });

      return tableOutPtr;
    }

  private:
    template <typename ElementType, typename AggregatorType>
    static void group(CompoundArray& tableOut,
                      std::shared_ptr<CompoundArray> const& srcBatchArrayPtr,
                      ElementType const& /*key*/, AggregatorType const& aggregator) {
      auto const& srcBatchArray = *srcBatchArrayPtr;
      size_t batchSize = srcBatchArray.length();
      if(batchSize == 0) {
        return;
      }

      // special case with only a unique key.
      // just aggregate on all the rows at once
      BulkExpressionArguments outputs;
      aggregate(outputs, aggregator, srcBatchArrayPtr);

      if(outputs.size() > 0) {
        tableOut.append(BulkComplexExpression(srcBatchArray.getHead(), std::move(outputs)));
      }
    }

    template <typename ElementType, typename AggregatorType>
    static void group(CompoundArray& tableOut,
                      std::shared_ptr<CompoundArray> const& srcBatchArrayPtr,
                      std::shared_ptr<ValueArray<ElementType>> const& keysPtr,
                      AggregatorType const& aggregator) {
      auto const& srcBatchArray = *srcBatchArrayPtr;
      size_t batchSize = srcBatchArray.length();
      if(batchSize == 0) {
        return;
      }

      // create sorted indexes
      // TODO: any way to reserve?
      using SortMap = std::conditional_t<std::is_same_v<ElementType, Symbol>,
                                         std::map<Symbol, std::vector<size_t>, CompareSymbolNames>,
                                         std::map<ElementType, std::vector<size_t>>>;
      SortMap sorted;
      auto keyIt = keysPtr->begin();
      for(size_t rowIndex = 0; rowIndex < batchSize; ++rowIndex, ++keyIt) {
        sorted[(ElementType)*keyIt].push_back(rowIndex);
      }

      BulkExpressionArguments outputs;
      aggregate(outputs, aggregator, srcBatchArray, sorted);

      if(outputs.size() > 0) {
        tableOut.append(BulkComplexExpression(srcBatchArray.getHead(), std::move(outputs)));
      }
    }

    template <typename AggregatorType>
    static void
    group(CompoundArray& /*tableOut*/, std::shared_ptr<CompoundArray> const& /*srcBatchArrayPtr*/,
          std::shared_ptr<CompoundArray> const& /*keysPtr*/, AggregatorType const& /*aggregator*/) {
      // [https://github.com/symbol-store/BOSS/issues/86]
      // how to do sorting if we handle list/expression as a key?
      // create a tuple of the values?
    }

    // aggregate for the general case where rows are sorted into groups
    template <typename AggregatorType, typename SortMap>
    static void aggregate(BulkExpressionArguments& outputs, AggregatorType const& aggregator,
                          CompoundArray const& srcArray, SortMap const& sorted) {
      auto groupedPtr = std::make_shared<CompoundArray>(srcArray, true);

      for(auto const& sortedIt : sorted) {
        // prepare the rows for the group to be processed
        auto const& rowIndices = sortedIt.second;
        OperatorUtils::insertRowValuesInOrder(*groupedPtr, srcArray, rowIndices);

        // evaluate the aggregate on the group
        BulkExpressionArguments inputs{groupedPtr};
        BulkExpression aggregatedOutput;
        if(Executor::evaluate(aggregator, inputs, aggregatedOutput)) {
          outputs.emplace_back(aggregatedOutput);
        }

        groupedPtr->clear();
      }
    }

    // aggregate for the special case where there is only a single group
    template <typename AggregatorType>
    static void aggregate(BulkExpressionArguments& outputs, AggregatorType const& aggregator,
                          std::shared_ptr<CompoundArray> const& srcArrayPtr) {
      // evaluate the aggregate on the input directly
      BulkExpressionArguments inputs{srcArrayPtr};
      BulkExpression aggregatedOutput;
      if(Executor::evaluate(aggregator, inputs, aggregatedOutput)) {
        outputs.emplace_back(aggregatedOutput);
      }
    }
  };
};

} // namespace boss::engines::bulk
