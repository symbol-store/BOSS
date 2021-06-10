#pragma once

#include "../ArrowExtensions/CompoundArray.hpp"
#include "../Executor.hpp"
#include "../ExpressionVisitDispatcher.hpp"
#include "../Operator.hpp"

#include <map>
#include <vector>

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Queries {
  using TableArgument = typename OperatorUtils::TableArgument;
  using FunctionArgument = typename OperatorUtils::FunctionArgument;

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<SelectOperator>("Select");
    operatorRegistry.template registerOperator<ProjectOperator>("Project");
    operatorRegistry.template registerOperator<SortByOperator>("SortBy");
    operatorRegistry.template registerOperator<GroupOperator>("Group");
  }

private:
  class SelectOperator : public Operator<TableArgument, FunctionArgument> {
  public:
    template <typename TableType, typename PredicateType>
    BulkExpression evaluate(TableType const& tableArrayPtr, PredicateType const& predicate) const {
      // copy with 'clear' flag, so we keep the (empty) column builders
      auto tableOutPtr = std::make_shared<CompoundArray>(*tableArrayPtr, true);
      auto& tableOut = *tableOutPtr;

      tableArrayPtr->visitPartitions([&tableOut, &predicate](auto&& batchOfRowsPtr) {
        // evaluate the predicate
        auto argsWithInputs = predicate.getArguments();
        argsWithInputs.emplace_back(BulkComplexExpression(Symbol("List"), {batchOfRowsPtr}));
        BulkComplexExpression predicateWithInputs(predicate.getHead(), argsWithInputs);
        auto toKeepOutput = Executor::evaluate(predicateWithInputs);

        // apply the predicate
        ExpressionVisitDispatcher<bool, std::shared_ptr<ValueArray<bool>>>::visit(
            [&tableOut, &batchOfRowsPtr](auto const& toKeep) {
              SelectOperator::select(tableOut, *batchOfRowsPtr, toKeep);
            },
            toKeepOutput);
      });

      return tableOutPtr;
    }

  private:
    static void select(CompoundArray& tableOut, CompoundArray const& srcArray,
                       std::shared_ptr<ValueArray<bool>> const& toKeepPtr) {
      OperatorUtils::insertRowValuesWithCondition(tableOut, srcArray, *toKeepPtr);
    }

    static void select(CompoundArray& tableOut, CompoundArray const& srcArray, bool toKeep) {
      // special case when toKeep is just a boolean
      // we can just do one single check and take all or nothing
      if(toKeep) {
        OperatorUtils::insertAllRows(tableOut, srcArray);
      }
    }
  };

  // projector: Function(tuple) return the list of columns we want to keep
  // e.g to project on the 1st column: "Function"_("tuple"_, "List"_("Column"_("tuple"_, 1)))
  class ProjectOperator : public Operator<TableArgument, FunctionArgument> {
  public:
    template <typename TableType, typename ProjectorType>
    BulkExpression evaluate(TableType const& tableArrayPtr, ProjectorType const& projector) const {
      // evaluate the projector function
      auto argsWithInputs = projector.getArguments();
      argsWithInputs.emplace_back(BulkComplexExpression(Symbol("List"), {tableArrayPtr}));
      BulkComplexExpression projectorWithInputs(projector.getHead(), argsWithInputs);
      auto projectedColumns = Executor::evaluate(projectorWithInputs);

      // retrieve the columns output by the projector
      // to return them as a table
      BulkExpression output;
      ExpressionVisitDispatcher<BulkComplexExpression>::visit(
          [&output](auto const& columns) { output = ProjectOperator::project(columns); },
          projectedColumns);
      return output;
    }

  private:
    static BulkExpression project(BulkComplexExpression const& columnList) {
      auto tableOutPtr = std::make_shared<CompoundArray>(true);
      auto& tableOut = *tableOutPtr;
      std::vector<ArrayData> argData;
      argData.reserve(columnList.getArguments().size());
      for(auto const& column : columnList.getArguments()) {
        OperatorUtils::CollectionVisitDispatcher::visit(
            [&argData](auto const& srcColumnPtr) { argData.emplace_back(srcColumnPtr->data()); },
            column);
      }
      tableOut.append(columnList.getHead(), argData);
      return tableOutPtr;
    }
  };

  // sortFunction: Function(tuple) return the key used for sorting
  // e.g to sort by first column: "Function"_(List_("tuple"_), "Column"_("tuple"_, 1))
  class SortByOperator : public Operator<TableArgument, FunctionArgument> {
  public:
    template <typename TableType, typename SortFunctionType>
    BulkExpression evaluate(TableType const& tableArrayPtr,
                            SortFunctionType const& sortFunction) const {
      // copy with 'clear' flag, so we keep the (empty) column builders
      auto tableOutPtr = std::make_shared<CompoundArray>(*tableArrayPtr, true);
      auto& tableOut = *tableOutPtr;

      tableArrayPtr->visitPartitions([&sortFunction, &tableOut](auto const& batchOfRowsPtr) {
        // evaluate the keys
        auto argsWithInputs = sortFunction.getArguments();
        argsWithInputs.emplace_back(BulkComplexExpression(Symbol("List"), {batchOfRowsPtr}));
        BulkComplexExpression sortFunctionWithInputs(sortFunction.getHead(), argsWithInputs);
        auto keysOutput = Executor::evaluate(sortFunctionWithInputs);

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
    static void sort(CompoundArray& tableOut, CompoundArray const& srcArray,
                     ElementType const& /*key*/) {
      // special case with only a single key...
      // just copy all the rows
      OperatorUtils::insertAllRows(tableOut, srcArray);
    }

    template <typename ElementType>
    static void sort(CompoundArray& tableOut, CompoundArray const& srcArray,
                     std::shared_ptr<ValueArray<ElementType>> const& keysPtr) {
      size_t ArraySize = srcArray.length();
      if(ArraySize == 0) {
        return;
      }

      // create sorted indexes
      // TODO: any way to reserve?
      using SortMap = std::conditional_t<std::is_same_v<ElementType, Symbol>,
                                         std::map<Symbol, std::vector<size_t>, CompareSymbolNames>,
                                         std::map<ElementType, std::vector<size_t>>>;
      SortMap sorted;
      auto keyIt = keysPtr->begin();
      for(size_t rowIndex = 0; rowIndex < ArraySize; ++rowIndex, ++keyIt) {
        sorted[(ElementType)*keyIt].push_back(rowIndex);
      }

      for(auto const& sortedIt : sorted) {
        auto const& rowIndices = sortedIt.second;
        OperatorUtils::insertRowValuesInOrder(tableOut, srcArray, rowIndices);
      }
    }

    static void sort(CompoundArray& tableOut, CompoundArray const& srcArray,
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
  class GroupOperator : public Operator<TableArgument, FunctionArgument, FunctionArgument> {
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
            auto argsWithInputs = groupFunction.getArguments();
            argsWithInputs.emplace_back(BulkComplexExpression(Symbol("List"), {batchOfRowsPtr}));
            BulkComplexExpression groupFunctionWithInputs(groupFunction.getHead(), argsWithInputs);
            auto keysOutput = Executor::evaluate(groupFunctionWithInputs);

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
    static void group(CompoundArray& tableOut, std::shared_ptr<CompoundArray> const& srcArrayPtr,
                      ElementType const& /*key*/, AggregatorType const& aggregator) {
      auto const& srcArray = *srcArrayPtr;
      size_t arraySize = srcArray.length();
      if(arraySize == 0) {
        return;
      }

      // special case with only a unique key.
      // just aggregate on all the rows at once
      BulkExpressionArguments outputs;
      aggregate(outputs, aggregator, srcArrayPtr);

      if(!outputs.empty()) {
        tableOut.append(BulkComplexExpression(srcArray.getHead(), outputs));
      }
    }

    template <typename ElementType, typename AggregatorType>
    static void group(CompoundArray& tableOut, std::shared_ptr<CompoundArray> const& srcArrayPtr,
                      std::shared_ptr<ValueArray<ElementType>> const& keysPtr,
                      AggregatorType const& aggregator) {
      auto const& srcArray = *srcArrayPtr;
      size_t arraySize = srcArray.length();
      if(arraySize == 0) {
        return;
      }

      // create sorted indexes
      // TODO: any way to reserve?
      using SortMap = std::conditional_t<std::is_same_v<ElementType, Symbol>,
                                         std::map<Symbol, std::vector<size_t>, CompareSymbolNames>,
                                         std::map<ElementType, std::vector<size_t>>>;
      SortMap sorted;
      auto keyIt = keysPtr->begin();
      for(size_t rowIndex = 0; rowIndex < arraySize; ++rowIndex, ++keyIt) {
        sorted[(ElementType)*keyIt].push_back(rowIndex);
      }

      BulkExpressionArguments outputs;
      aggregate(outputs, aggregator, srcArray, sorted);

      if(!outputs.empty()) {
        tableOut.append(BulkComplexExpression(srcArray.getHead(), outputs));
      }
    }

    template <typename AggregatorType>
    static void
    group(CompoundArray& /*tableOut*/, std::shared_ptr<CompoundArray> const& /*srcArrayPtr*/,
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

      outputs.reserve(outputs.size() + sorted.size());
      for(auto const& sortedIt : sorted) {
        // prepare the rows for the group to be processed
        auto const& rowIndices = sortedIt.second;
        OperatorUtils::insertRowValuesInOrder(*groupedPtr, srcArray, rowIndices);

        // evaluate the aggregate on the group
        auto argsWithInputs = aggregator.getArguments();
        argsWithInputs.emplace_back(BulkComplexExpression(Symbol("List"), {groupedPtr}));
        BulkComplexExpression aggregatorWithInputs(aggregator.getHead(), argsWithInputs);
        outputs.emplace_back(Executor::evaluate(aggregatorWithInputs));

        groupedPtr->clear();
      }
    }

    // aggregate for the special case where there is only a single group
    template <typename AggregatorType>
    static void aggregate(BulkExpressionArguments& outputs, AggregatorType const& aggregator,
                          std::shared_ptr<CompoundArray> const& srcArrayPtr) {
      // evaluate the aggregate on the input directly
      auto argsWithInputs = aggregator.getArguments();
      argsWithInputs.emplace_back(BulkComplexExpression(Symbol("List"), {srcArrayPtr}));
      BulkComplexExpression aggregatorWithInputs(aggregator.getHead(), argsWithInputs);
      outputs.emplace_back(Executor::evaluate(aggregatorWithInputs));
    }
  };
};

} // namespace boss::engines::bulk
