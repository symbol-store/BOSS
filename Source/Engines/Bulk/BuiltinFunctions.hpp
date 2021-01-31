#pragma once

#include "Batch/FunctionBatch.hpp"
#include "BatchTemplates.hpp"
#include "SymbolPool.hpp"
#include "TableView.hpp"

#include "../../Expression.hpp"
#include "../../Utilities.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace boss::engines::bulk {

using boss::utilities::operator""_;

/****************** class BuiltinFunctions ********************/

/* Helper class just for registering all the builin functions */
/**************************************************************/

template <typename BatchTemplates> class BuiltinFunctions {
public:
  using AnyBatch = typename BatchTemplates::AnyBatch;
  using BatchHelperAny = typename BatchTemplates::BatchHelper;

  static void registerAll(BatchTemplates& templates) {
    arithmetic(templates);
    comparison(templates);
    logic(templates);
    symbolicOps(templates);
    collections(templates);
    aggregates(templates);
    dbManagement(templates);
    queries(templates);
  }

private:
  static void arithmetic(BatchTemplates& templates) {
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Plus", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Minus", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Times", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Divide", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatch,
              rhsBatch);
        });
  };

  static void comparison(BatchTemplates& templates) {
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Equal", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a == b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "NotEqual", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a != b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Less", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a < b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "LessEqual", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a <= b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Greater", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a > b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "GreaterEqual", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a >= b; }, lhsBatch,
              rhsBatch);
        });
  }

  static void logic(BatchTemplates& templates) {
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "And", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a && b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "Or", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a || b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<bool>().template registerFunction<1>(
        "Not", [&templates](auto const& batch) {
          return evaluateElements(
              templates, [](auto const& a) -> bool { return !a; }, batch);
        });

    // Strings
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringJoin", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> std::string { return a + b; },
              lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringContainsQ", [&templates](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              templates,
              [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
              lhsBatch, rhsBatch);
        });
  }

  static void symbolicOps(BatchTemplates& templates) {
    auto& symbolPool = DefaultSymbolPool::instance();

    templates.template allowedTypes<std::string>().template registerFunction<1>(
        "Symbol", [&templates](auto const& batch) {
          return evaluateElements(
              templates, [](auto const& name) -> Symbol { return Symbol(name); }, batch);
        });

    templates.template argBatchTypes<SymbolBatch, AnyBatch>().template registerFunction<2>(
        "Set", [&symbolPool](auto const& symbolBatch, auto const& rhsBatch) {
          using SymbolBatchType = std::decay_t<decltype(symbolBatch)>;
          if constexpr(std::conjunction_v<std::is_same<SymbolBatchType, SymbolBatch>,
                                          std::is_same<SymbolBatchType, ValueBatch<std::string>>,
                                          std::is_same<SymbolBatchType, RLEBatch<std::string>>>) {
            auto const& symbol = Symbol(*symbolBatch.begin());
            symbolPool.findSymbol(symbol) = std::move(rhsBatch.clone());
            return BatchPtr(new SymbolBatch(1, symbol));
          }
          return BatchPtr(new SymbolBatch(1));
        });

    templates.template argBatchTypes<AnyBatch>().template registerFunction<1>(
        "Function", [&templates](auto const& definition) {
          return BatchPtr(new FunctionBatch(templates, std::vector<Symbol>{}, definition));
        });

    templates.template argBatchTypes<CompoundBatch, AnyExpressionBatch>()
        .template registerFunction<2>(
            "Function", [&templates](auto const& argBatch, auto const& definition) {
              std::vector<Symbol> args;
              args.reserve(argBatch.size());
              for(auto const& symbolBatchPtr : argBatch) {
                if(symbolBatchPtr->typeId() == UniqueId::forType<SymbolBatch>()) {
                  args.emplace_back(*static_cast<SymbolBatch*>(symbolBatchPtr.get())->begin());
                }
              }
              return BatchPtr(new FunctionBatch(templates, args, definition));
            });
  }

  static void collections(BatchTemplates& templates) {
    templates.template argBatchTypes<AnyBatch, RLEBatch<int>>().template registerFunction<2>(
        "Extract", [&templates](auto const& batchExpr, auto const& batchNth) {
          using BatchType = std::decay_t<decltype(batchExpr)>;
          using ValueType = typename BatchType::ValueType;
          size_t index = *batchNth.begin() - 1;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            return batchExpr.extract(index);
          } else {
            auto const& value = static_cast<ValueType>(*(batchExpr.begin() + index));
            auto batchPtr = templates.createBatch(value);
            batchPtr->insert(value);
            return batchPtr;
          }
        });

    templates.template argBatchTypes<CompoundBatch, RLEBatch<int>>().template registerFunction<2>(
        "Column", [](auto const& batchExpr, auto const& batchNth) {
          size_t index = *batchNth.begin() - 1;
          return batchExpr.reduce(index);
        });

    templates.template argBatchTypes<AnyBatch>().template registerFunction<1>(
        "Length",
        [](auto const& batchExpr) { return BatchPtr(new RLEBatch<int>(1, batchExpr.size())); });
  }

  static void aggregates(BatchTemplates& templates) {
    templates.template argBatchTypes<AnyBatch>().template registerFunction<1>(
        "Count", [](auto const& batch) { return BatchPtr(new RLEBatch<int>(1, batch.size())); });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Sum", [](auto const& batch) {
          auto it = batch.begin();
          auto sum = *it;
          ++it;
          while(it != batch.end()) {
            sum += *it;
            ++it;
          }
          return BatchPtr(new RLEBatch<decltype(sum)>(sum));
        });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Min", [](auto const& batch) {
          auto it = batch.begin();
          auto min = *it;
          ++it;
          while(it != batch.end()) {
            auto value = *it;
            if(value < min) {
              min = value;
            }
            ++it;
          }
          return BatchPtr(new RLEBatch<decltype(min)>(min));
        });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Max", [](auto const& batch) {
          auto it = batch.begin();
          auto max = *it;
          ++it;
          while(it != batch.end()) {
            auto value = *it;
            if(value > max) {
              max = value;
            }
            ++it;
          }
          return BatchPtr(new RLEBatch<decltype(max)>(max));
        });
  }

  static void dbManagement(BatchTemplates& templates) {
    auto& tableViewPool = WritableBatchPool::instance();

    templates.template argTypes<Symbol>().template registerFunction<1>(
        "CreateTable", [&templates, &tableViewPool](auto const& batch) {
          return evaluateElements(
              templates,
              [&templates, &tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr = WritableBatchPool::SymbolPtr(new TableView(templates));
                return table;
              },
              batch);
        });

    templates.template argTypes<Symbol>().template registerFunction<1>(
        "RemoveTable", [&templates, &tableViewPool](auto const& batch) {
          return evaluateElements(
              templates,
              [&tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr.reset();
                return table;
              },
              batch);
        });

    templates.template argTypes<Symbol, std::string>().template registerFunction<2>(
        "AddColumn", [&templates, &tableViewPool](auto const& tableBatch, auto const& columnBatch) {
          return evaluateElements(
              templates,
              [&tableViewPool](auto const& table, auto const& columnName) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
                  auto& tableView = *static_cast<TableView*>(symbolPtr.get());
                  tableView.addColumn(columnName);
                }
                return table;
              },
              tableBatch, columnBatch);
        });

    templates.template argTypes<Symbol, ComplexExpression>().template registerFunction<2>(
        "InsertInto", [&templates, &tableViewPool](auto const& symbolBatch, auto const& rowBatch) {
          auto& symbolPtr = tableViewPool.findSymbol(*symbolBatch.begin());
          if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
            auto& tableView = *symbolPtr;
            tableView.insert("List"_(templates.revertToExpression(rowBatch)));
          }
          return symbolBatch.clone();
        });
  }

  static void queries(BatchTemplates& templates) {
    selection(templates);
    projection(templates);
    sorting(templates);
    grouping(templates);
  }

  static void selection(BatchTemplates& templates) {
    auto select = [](auto const& tableView, auto const& predicate) -> BatchPtr {
      auto tableOutPtr = tableView.cloneAsTableView(true);
      auto& tableOut = *tableOutPtr;

      auto forEachBatchOfRows = [&tableOut](auto const& tableKey, CompoundBatch const& batch,
                                            auto const& toKeep) {
        auto batchOutPtr = batch.cloneAsCompoundBatch(true);
        auto& batchOut = *batchOutPtr;

        batch.visitBatches<BatchHelperAny>(
            [&toKeep, &batchOut](auto const& columnKey, auto const& column) {
              using ColumnBatchType = std::decay_t<decltype(column)>;
              using ValueType = typename ColumnBatchType::ValueType;
              auto newColumnBatchPtr = column.clone(true);
              auto& newColumnBatch = *static_cast<ColumnBatchType*>(newColumnBatchPtr.get());
              newColumnBatch.resize(column.size(), columnKey.first); // pessimistic
              size_t newColumnSize = copyRowValuesWithCondition(newColumnBatch, column, toKeep);
              if(newColumnSize > 0) {
                newColumnBatch.resize(newColumnSize, columnKey.first); // shrink it back
                batchOut.insert(columnKey.first, columnKey.second, std::move(newColumnBatchPtr));
              }
            });

        if(batchOut.size() > 0) {
          tableOut.insert(tableKey.first, tableKey.second, std::move(batchOutPtr));
        }
      };

      tableView.template visitBatches<BatchHelper<CompoundBatch>>(
          [&predicate, &forEachBatchOfRows](auto const& tableKey, auto const& batchOfRows) {
            auto toKeepPtr = predicate.evaluateWith(std::vector<Batch const*>{&batchOfRows});
            auto const& toKeep = *toKeepPtr;
            BatchHelper<ValueBatch<bool>, RLEBatch<bool>>::visit(
                [&](auto const& toKeepAsBool) {
                  forEachBatchOfRows(tableKey, batchOfRows, toKeepAsBool);
                },
                toKeep);
          });

      return tableOutPtr;
    };

    templates.template argBatchTypes<SymbolBatch, FunctionBatch>().template registerFunction<2>(
        "Select", [select](auto const& symbolBatch, auto const& predicate) {
          return runQueryOp(select, "Select", symbolBatch, predicate);
        });
  }

  static void projection(BatchTemplates& templates) {
    auto project = [&templates](auto const& tableView, auto const& columns) -> BatchPtr {
      TableView::TableViewPtr tableOutPtr(
          new TableView(templates)); // not a clone so we clear columns too
      auto& tableOut = *tableOutPtr;

      // fill the indexes
      std::vector<size_t> indexes;
      indexes.reserve(columns.size());
      for(auto& columnBatchPtr : columns) {
        BatchHelper<ValueBatch<std::string>, RLEBatch<std::string>>::visit(
            [&indexes, &tableView, &tableOut](auto& columnBatch) {
              for(auto const& columnName : columnBatch) {
                int index = tableView.columnIndex(columnName);
                if(index < 0) {
                  continue;
                }
                indexes.push_back(index);
                tableOut.addColumn(columnName);
              }
            },
            *columnBatchPtr);
      }

      // copy the new batches from the indexes
      if(!indexes.empty()) {
        tableView.template visitBatches<BatchHelper<CompoundBatch>>(
            [&indexes, &tableOut](auto const& tableKey, auto const& oldColumns) {
              auto newColumnsPtr = oldColumns.cloneAsCompoundBatch(true);
              auto& newColumns = *newColumnsPtr;
              auto const& columnKeyExpression = std::get<ComplexExpression>(tableKey.first);
              auto const& columnKeyArgs = columnKeyExpression.getArguments();
              size_t newIndex = 0;
              for(size_t index : indexes) {
                auto columnBatchPtr = oldColumns.at(index)->clone();
                auto& columnKeyArg = columnKeyArgs[index];
                newColumns.insert(columnKeyArg, newIndex++, std::move(columnBatchPtr));
              }
              tableOut.insert(tableKey.first, tableKey.second, std::move(newColumnsPtr));
            });
      }
      return tableOutPtr;
    };

    templates.template argBatchTypes<SymbolBatch, CompoundBatch>().template registerFunction<2>(
        "Project", [project](auto const& symbolBatch, auto const& columns) {
          return runQueryOp(project, "Project", symbolBatch, columns);
        });
  }

  static void sorting(BatchTemplates& templates) {
    // sortFunction: Function(tuple) return the key used for sorting
    // e.g to sort by first column: "Function"_(List_("tuple"_), "Column"_("tuple"_, 1))
    auto sortBy = [](auto const& tableView, auto const& sortFunction) -> BatchPtr {
      auto tableOutPtr = tableView.cloneAsTableView(true);
      auto& tableOut = *tableOutPtr;

      auto forEachBatchOfRows = [&tableOut](auto const& tableKey, CompoundBatch const& batch,
                                            auto& keys) {
        using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

        // create sorted indexes
        // TODO: any way to reserve?
        std::map<ElementType, std::vector<size_t>> sorted;
        size_t batchSize = batch.size();
        auto keyIt = keys.begin();
        for(size_t rowIndex = 0; rowIndex < batchSize; ++rowIndex, ++keyIt) {
          sorted[*keyIt].push_back(rowIndex);
        }

        auto batchOutPtr = batch.cloneAsCompoundBatch(true);
        auto& batchOut = *batchOutPtr;
        batch.visitBatches<BatchHelperAny>(
            [&batchOut, &sorted](auto const& columnKey, auto const& column) {
              using ColumnBatchType = std::decay_t<decltype(column)>;
              auto newColumnBatchPtr = column.clone(true);
              auto& newColumnBatch = *static_cast<ColumnBatchType*>(newColumnBatchPtr.get());
              newColumnBatch.resize(column.size(), columnKey.first);
              auto newcolumnIt = newColumnBatch.begin();
              auto newcolumnItEnd = newColumnBatch.end();
              for(auto const& sortedIt : sorted) {
                auto const& rowIndices = sortedIt.second;
                copyRowValuesInOrder(newcolumnIt, newcolumnItEnd, column, rowIndices);
              }
              batchOut.insert(columnKey.first, columnKey.second, std::move(newColumnBatchPtr));
            });
        tableOut.insert(tableKey.first, tableKey.second, std::move(batchOutPtr));
      };

      tableView.template visitBatches<BatchHelper<CompoundBatch>>(
          [&sortFunction, &forEachBatchOfRows](auto const& tableKey, auto const& batchOfRows) {
            auto keysPtr = sortFunction.evaluateWith(std::vector<Batch const*>{&batchOfRows});
            auto const& keys = *keysPtr;
            if(keys.size() == 0) {
              return;
            }
            BatchHelperAny::visit(
                [&](auto const& specificKeys) {
                  using KeyBatchType = std::decay_t<decltype(specificKeys)>;
                  if constexpr(!std::is_base_of_v<CompoundBatch, KeyBatchType>) {
                    forEachBatchOfRows(tableKey, batchOfRows, specificKeys);
                  } else {
                    // TODO: how to do sorting if we handle list as a key?
                    // create a tuple of the values?
                  }
                },
                keys);
          });
      return tableOutPtr;
    };

    templates.template argBatchTypes<SymbolBatch, FunctionBatch>().template registerFunction<2>(
        "SortBy", [sortBy](auto const& symbolBatch, auto const& sortFunction) {
          return runQueryOp(sortBy, "SortBy", symbolBatch, sortFunction);
        });
  }

  static void grouping(BatchTemplates& templates) {
    // groupFunction: Function(tuple) return a key
    // e.g to group by first column: "Function"_(List_("tuple"_), "Extract"_("tuple"_,
    // 1)) aggregator: Function("tuple", "aggregateResult") return the aggregate result
    // e.g to count: "Function"_(List_("tuple"_), "Count"_("Extract"_("tuple"_, 1)))
    // e.g to sum: "Function"_(List_("tuple"_), "Sum_("Extract"_("tuple"_, 1)))
    // e.g to return the key: "Function"_(List_("tuple"_), "Extract"_("tuple"_, 1))
    auto groupBy = [&templates](auto const& tableView, auto const& groupFunction,
                                auto const& aggregator) -> BatchPtr {
      auto tableOutPtr =
          TableView::TableViewPtr(new TableView(templates)); // not a clone so we clear columns too
      auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());

      auto aggregate = [&aggregator](auto& destbatch, auto const& srcBatch, auto const& sorted) {
        auto groupedPtr = srcBatch.cloneAsCompoundBatch(true);
        auto& grouped = *groupedPtr;
        for(auto const& sortedIt : sorted) {
          auto const& rowIndices = sortedIt.second;
          srcBatch.template visitBatches<BatchHelperAny>(
              [&grouped, &rowIndices](auto const& columnKey, auto const& column) {
                using ColumnBatchType = std::decay_t<decltype(column)>;
                auto newColumnBatchPtr = column.clone(true);
                auto& newColumnBatch = *static_cast<ColumnBatchType*>(newColumnBatchPtr.get());
                newColumnBatch.resize(rowIndices.size(), columnKey.first);
                auto newColumnBatchIt = newColumnBatch.begin();
                auto newColumnBatchItEnd = newColumnBatch.end();
                copyRowValuesInOrder(newColumnBatchIt, newColumnBatchItEnd, column, rowIndices);
                grouped.insert(columnKey.first, columnKey.second, std::move(newColumnBatchPtr));
              });

          // process to be called for each group of (sorted) table rows
          auto aggregatedBatchPtr = aggregator.evaluateWith(std::vector<Batch const*>{&grouped});
          destbatch.emplace_back(std::move(aggregatedBatchPtr));
          grouped.clear();
        }
      };

      auto InsertRows = [&tableOut](auto const& batch) {
        // TODO: set proper column names
        size_t batchSize = batch.baseId() == UniqueId::forType<CompoundBatch>() ? batch.size() : 1;
        for(size_t colIndex = tableOut.numColumns(); colIndex < batch.size(); ++colIndex) {
          tableOut.addColumn("aggr" + std::to_string(++colIndex));
        }
      };

      typename CompoundBatch::BatchList newBatches;
      auto forEachBatchOfRows = [&newBatches, &aggregate, &InsertRows](auto const& /*tableKey*/,
                                                                       CompoundBatch const& batch,
                                                                       auto const& keys) {
        using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

        // create sorted indexes
        // TODO: any way to reserve?
        std::map<ElementType, std::vector<size_t>> sorted;
        size_t batchSize = batch.size();
        auto keyIt = keys.begin();
        for(size_t rowIndex = 0; rowIndex < batchSize; ++rowIndex, ++keyIt) {
          sorted[*keyIt].push_back(rowIndex);
        }

        newBatches.reserve(newBatches.size() + keys.size());
        aggregate(newBatches, batch, sorted);
        if(newBatches.empty()) {
          return;
        }

        auto const& firstBatch = *newBatches.front(); // any would do
        InsertRows(firstBatch);
      };

      tableView.template visitBatches<BatchHelper<CompoundBatch>>(
          [&groupFunction, &forEachBatchOfRows](auto const& tableKey, auto const& batchOfRows) {
            auto keysPtr = groupFunction.evaluateWith(std::vector<Batch const*>{&batchOfRows});
            auto& keys = *keysPtr;
            if(keys.size() == 0) {
              return;
            }
            BatchHelperAny::visit(
                [&](auto const& specificKeys) {
                  using KeyBatchType = std::decay_t<decltype(specificKeys)>;
                  if constexpr(!std::is_base_of_v<CompoundBatch, KeyBatchType>) {
                    forEachBatchOfRows(tableKey, batchOfRows, specificKeys);
                  } else {
                    // TODO: how to do sorting if we handle list as a key?
                    // create a tuple of the values?
                  }
                },
                keys);
          });

      for(auto& newBatchPtr : newBatches) {
        tableOut.insert("List"_(templates.revertToExpression(*newBatchPtr)));
      }
      return tableOutPtr;
    };

    templates.template argBatchTypes<SymbolBatch, FunctionBatch, FunctionBatch>()
        .template registerFunction<3>(
            "GroupBy",
            [groupBy](auto const& symbolBatch, auto const& groupFunction, auto const& aggregator) {
              return runQueryOp(groupBy, "GroupBy", symbolBatch, groupFunction, aggregator);
            });
  }

  // helpers to retrieve return type for a specific set of Batch argument types
  template <typename T> using FromBatchTypeToElementType = typename T::ValueType;
  template <typename T> struct BatchIsRLE { static constexpr auto value = T::IsRLE::value; };
  template <typename Func, typename... BatchTypes>
  using ReturnType = typename std::invoke_result_t<Func, FromBatchTypeToElementType<BatchTypes>...>;

  // helpers to iterate and evaluate on each element of a batch
  template <typename Func, typename... BatchIn>
  static BatchPtr evaluateElements(BatchTemplates& templates, Func&& func, BatchIn const&... in) {
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
      apply(*outputBatch, in.begin()...);
      return BatchPtr(outputBatch);
    } else if constexpr(std::is_same_v<ReturnType, ComplexExpression>) {
      auto* outputBatch = new CompoundBatch(templates);
      apply(*outputBatch, in.begin()...);
      return BatchPtr(outputBatch);
    } else {
      size_t outputSize = 1;
      (..., [&outputSize, &in]() { outputSize = std::max(outputSize, in.size()); }());
      if constexpr((... && BatchIsRLE<std::decay_t<decltype(in)>>::value)) {
        auto* outputBatch = new RLEBatch<ReturnType>(outputSize, ReturnType());
        apply(*outputBatch, in.begin()...);
        return BatchPtr(outputBatch);
      } else {
        auto* outputBatch = new ValueBatch<ReturnType>(outputSize, ReturnType());
        apply(*outputBatch, in.begin()...);
        return BatchPtr(outputBatch);
      }
    }
  }

  template <typename Func, typename... BatchArgs>
  static BatchPtr runQueryOp(Func&& opFunc, std::string const& opName,
                             SymbolBatch const& symbolBatch, BatchArgs const&... batchArgs) {
    auto& tableViewPool = WritableBatchPool::instance();

    auto const& symbol = *symbolBatch.begin();
    auto& symbolPtr = tableViewPool.findSymbol(symbol);
    if(!symbolPtr) {
      return symbolBatch.clone();
    }

    BatchPtr queryResult;
    BatchHelper<TableView>::visit(
        [&](auto& tableView) { queryResult = opFunc(tableView, batchArgs...); }, *symbolPtr);
    if(!queryResult) {
      return symbolBatch.clone();
    }

    // save the query result into a temporary symbol
    // this is a workaround to avoid transferring the whole result between query ops
    // (and which would cause an unevaluated call to return the whole table...)
    // TODO: find a better way to handle this
    static int i = 0;
    Symbol tempSymbol(symbol.getName() + "_" + opName + std::to_string(i++));
    auto& tempBatchPtr = tableViewPool.findSymbol(tempSymbol);
    tempBatchPtr = std::move(queryResult);
    return BatchPtr(new SymbolBatch(1, tempSymbol));
  }

  template <typename DestBatchIterator, typename SrcBatchType>
  static void copyRowValuesInOrder(DestBatchIterator& destBatchIt,
                                   DestBatchIterator& destBatchItEnd, SrcBatchType const& srcBatch,
                                   std::vector<size_t> const& rowIndices) {
    // copy row values in sorted order, column per column
    // TODO: insert together consecutive rows
    // (or directly re-implement with a more efficient method...)
    for(auto rowIndexIt = rowIndices.begin();
        rowIndexIt != rowIndices.end() && destBatchIt != destBatchItEnd;
        ++rowIndexIt, ++destBatchIt) {
      auto columnIt = srcBatch.begin() + *rowIndexIt;
      auto const& value = *columnIt;
      if constexpr(std::is_base_of_v<BatchPtr, std::decay_t<decltype(value)>>) {
        *destBatchIt = value->clone();
      } else {
        *destBatchIt = value;
      }
    }
  }

  template <typename DestBatchType, typename SrcBatchType, typename ConditionBatchType>
  static size_t copyRowValuesWithCondition(DestBatchType& destBatch, SrcBatchType const& srcBatch,
                                           ConditionBatchType const& conditionBatch) {
    size_t numRows = 0;
    auto columnIt = srcBatch.begin();
    auto conditionIt = conditionBatch.begin();
    auto newColumnIt = destBatch.begin();
    for(; columnIt != srcBatch.end() && newColumnIt != destBatch.end(); ++columnIt, ++conditionIt) {
      if(!*conditionIt) {
        continue;
      }
      auto const& value = *columnIt;
      if constexpr(std::is_base_of_v<BatchPtr, std::decay_t<decltype(value)>>) {
        *newColumnIt = value->clone();
      } else {
        *newColumnIt = value;
      }
      ++newColumnIt;
      ++numRows;
    }
    return numRows;
  }
};

} // namespace boss::engines::bulk
