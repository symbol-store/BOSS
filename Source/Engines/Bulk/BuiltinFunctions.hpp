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

template <typename... SupportedTypes> class BuiltinFunctions {
public:
  using BatchTemplates = BatchTemplates<SupportedTypes...>;
  using BatchHelperAny = typename BatchTemplates::BatchHelper;

  explicit BuiltinFunctions(BatchTemplates& templates) : m_templates(templates) {
    auto& symbolPool = DefaultSymbolPool::instance();
    auto& tableViewPool = WritableBatchPool::instance();

    // Arithmetic
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Plus", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Minus", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Times", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Divide", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatch, rhsBatch);
        });

    // Comparison
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Equal", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a == b; },
                                  lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "NotEqual", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a != b; },
                                  lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Less", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a < b; },
                                  lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "LessEqual", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a <= b; },
                                  lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Greater", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a > b; },
                                  lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "GreaterEqual", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a >= b; },
                                  lhsBatch, rhsBatch);
        });

    // Logic
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "And", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a && b; },
                                  lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "Or", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a || b; },
                                  lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool>().template registerFunction<1>(
        "Not", [this](auto const& batch) {
          return evaluateElements([](auto const& a) -> bool { return !a; }, batch);
        });

    // Strings
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringJoin", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements([](auto const& a, auto const& b) -> std::string { return a + b; },
                                  lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringContainsQ", [this](auto const& lhsBatch, auto const& rhsBatch) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
              lhsBatch, rhsBatch);
        });

    // Symbolic
    templates.template allowedTypes<std::string>().template registerFunction<1>(
        "Symbol", [this](auto const& batch) {
          return evaluateElements([](auto const& name) -> Symbol { return Symbol(name); }, batch);
        });
    templates.template allowedTypes<int, float, bool, std::string, ComplexExpression>()
        .template registerFunction<2>("Set", [this, &symbolPool, &templates](
                                                 auto const& symbolBatch, auto const& rhsBatch) {
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
              // TODO: ideally FunctionBatch should derive from ExpressionBatch
              // and here we should call batch template
              // to get the specific FunctionBatch for an evaluator + parameter list
              return BatchPtr(new FunctionBatch(templates, args, definition));
            });
    // temporarly needed until Function/Set can support any batch type as definition
    templates.template allowedTypes<bool, int, float, std::string>().template registerFunction<1>(
        "Constant", [this](auto const& batch) {
          return evaluateElements(
              [](auto const& a) -> auto { return a; }, batch);
        });

    // Collections
    templates.template argBatchTypes<CompoundBatch, RLEBatch<int>>().template registerFunction<2>(
        "Extract", [](auto const& batchExpr, auto const& batchNth) {
          size_t index = *batchNth.begin() - 1;
          return batchExpr.extract(index);
        });
    templates.template argBatchTypes<CompoundBatch, RLEBatch<int>>().template registerFunction<2>(
        "Column", [](auto const& batchExpr, auto const& batchNth) {
          size_t index = *batchNth.begin() - 1;
          return batchExpr.reduce(index);
        });
    templates.any().template registerFunction<1>("Length", [](auto const& batchExpr) {
      return BatchPtr(new RLEBatch<int>(1, batchExpr.size()));
    });

    // Aggregates
    templates.template allowedTypes<bool, int, float, std::string, ComplexExpression>()
        .template registerFunction<1>("Count", [](auto const& batch) {
          return BatchPtr(new RLEBatch<int>(1, batch.size()));
        });
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

    // Database & Queries
    templates.template argTypes<Symbol>().template registerFunction<1>(
        "CreateTable", [this, &templates, &tableViewPool](auto const& batch) {
          return evaluateElements(
              [&templates, &tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr = WritableBatchPool::SymbolPtr(new TableView(templates));
                return table;
              },
              batch);
        });
    templates.template argTypes<Symbol>().template registerFunction<1>(
        "RemoveTable", [this, &templates, &tableViewPool](auto const& batch) {
          return evaluateElements(
              [&templates, &tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr.reset();
                return table;
              },
              batch);
        });
    templates.template argBatchTypes<SymbolBatch, RLEBatch<std::string>>()
        .template registerFunction<2>("AddColumn", [this, &templates,
                                                    &tableViewPool](auto const& tableBatch,
                                                                    auto const& columnBatch) {
          return evaluateElements(
              [&templates, &tableViewPool](auto const& table, auto const& columnName) -> Symbol {
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
        "InsertInto",
        [this, &templates, &tableViewPool](auto const& symbolBatch, auto const& rowBatch) {
          auto& symbolPtr = tableViewPool.findSymbol(*symbolBatch.begin());
          if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
            auto& tableView = *symbolPtr;
            tableView.insert("List"_(templates.revertToExpression(rowBatch)));
          }
          return symbolBatch.clone();
        });
    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "Select", [&templates](auto const& tableView, auto const& predicate) {
          auto tableOutPtr = tableView.clone(true);
          auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());

          auto forEachBatchOfRows = [&templates, &tableOut](auto const& tableKey,
                                                            CompoundBatch const& batch,
                                                            auto const& toKeep) {
            auto batchOutPtr = batch.clone(true);
            auto& batchOut = *static_cast<CompoundBatch*>(batchOutPtr.get());

            batch.visitBatches([&](auto const& columnKey, Batch const& genericColumn) {
              if(genericColumn.size() == 0) {
                return;
              }
              BatchHelperAny::visit(
                  [&](auto const& column) {
                    using ColumnBatchType = std::decay_t<decltype(column)>;
                    auto newColumnBatchPtr = column.clone(true);
                    auto& newColumnBatch = *static_cast<ColumnBatchType*>(newColumnBatchPtr.get());
                    newColumnBatch.resize(column.size(), columnKey.first); // pessimistic
                    auto columnIt = column.begin();
                    auto toKeepIt = toKeep.begin();
                    auto newColumnIt = newColumnBatch.begin();
                    size_t newColumnSize = 0;
                    for(; columnIt != column.end(); ++columnIt, ++toKeepIt) {
                      if(*toKeepIt) {
                        auto const& value = *columnIt;
                        if constexpr(std::is_base_of_v<BatchPtr, std::decay_t<decltype(value)>>) {
                          *newColumnIt = value->clone();
                        } else {
                          *newColumnIt = value;
                        }
                        ++newColumnIt;
                        ++newColumnSize;
                      }
                    }
                    if(newColumnSize > 0) {
                      newColumnBatch.resize(newColumnSize, columnKey.first); // shrink it back
                      batchOut.insert(columnKey.first, columnKey.second,
                                      std::move(newColumnBatchPtr));
                    }
                  },
                  genericColumn);
            });

            if(batchOut.size() > 0) {
              tableOut.insert(tableKey.first, tableKey.second, std::move(batchOutPtr));
            }
          };

          tableView.visitBatches(
              [&predicate, &forEachBatchOfRows](auto const& tableKey, Batch const& batch) {
                if(batch.baseId() == UniqueId::forType<CompoundBatch>()) {
                  auto toKeepPtr = predicate.evaluateWith(std::vector<Batch const*>{&batch});
                  auto& toKeep = *toKeepPtr;
                  if(toKeep.typeId() == UniqueId::forType<ValueBatch<bool>>()) {
                    forEachBatchOfRows(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                       *static_cast<ValueBatch<bool>*>(&toKeep));
                  } else if(toKeep.typeId() == UniqueId::forType<RLEBatch<bool>>()) {
                    forEachBatchOfRows(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                       *static_cast<RLEBatch<bool>*>(&toKeep));
                  }
                }
              });

          return tableOutPtr;
        });

    templates.template argBatchTypes<TableView, CompoundBatch>().template registerFunction<2>(
        "Project", [&templates](auto const& tableView, auto const& columns) {
          auto tableOutPtr =
              BatchPtr(new TableView(templates)); // not a clone so we clear columns too
          auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());
          // fill the indexes
          std::vector<size_t> indexes;
          indexes.reserve(columns.size());
          for(auto& columnBatchPtr : columns) {
            BatchHelper<ValueBatch<std::string>, RLEBatch<std::string>>::visit(
                [&indexes, &tableView, &tableOut](auto& columnBatch) {
                  for(auto const& columnName : columnBatch) {
                    int index = tableView.columnIndex(columnName);
                    if(index >= 0) {
                      indexes.push_back(index);
                      tableOut.addColumn(columnName);
                    }
                  }
                },
                *columnBatchPtr);
          }
          // copy the new batches from the indexes
          if(!indexes.empty()) {
            tableView.visitBatches(
                [&templates, &indexes, &tableOut](auto const& tableKey, Batch const& batch) {
                  if(batch.baseId() == UniqueId::forType<CompoundBatch>()) {
                    auto const& oldColumns = *static_cast<CompoundBatch const*>(&batch);
                    auto newColumnsPtr = oldColumns.clone(true);
                    auto& newColumns = *static_cast<CompoundBatch*>(newColumnsPtr.get());
                    auto const& columnKeyExpression = std::get<ComplexExpression>(tableKey.first);
                    auto const& columnKeyArgs = columnKeyExpression.getArguments();
                    size_t newIndex = 0;
                    for(size_t index : indexes) {
                      auto columnBatchPtr = oldColumns.at(index)->clone();
                      auto& columnKeyArg = columnKeyArgs[index];
                      newColumns.insert(columnKeyArg, newIndex++, std::move(columnBatchPtr));
                    }
                    tableOut.insert(tableKey.first, tableKey.second, std::move(newColumnsPtr));
                  }
                });
          }
          return tableOutPtr;
        });
    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "SortBy",
        // sortFunction: Function(tuple) return the key used for sorting
        // e.g to sort by first column: "Function"_(List_("tuple"_), "Column"_("tuple"_, 1))
        [&templates](auto const& tableView, auto const& sortFunction) {
          auto tableOutPtr = tableView.clone(true);
          auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());

          auto forEachBatchOfRows = [&templates, &tableOut](auto const& tableKey,
                                                            CompoundBatch const& batch,
                                                            auto& keys) {
            using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

            // create sorted indexes
            std::vector<std::pair<ElementType, size_t>> toSort;
            size_t batchSize = batch.size();
            toSort.reserve(batchSize);
            for(auto [keyIt, rowIndex] = std::pair{keys.begin(), size_t(0)}; rowIndex < batchSize;
                ++keyIt, ++rowIndex) {
              toSort.push_back(std::make_pair(*keyIt, rowIndex));
            }
            std::sort(toSort.begin(), toSort.end(),
                      [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });

            auto batchOutPtr = batch.clone(true);
            auto& batchOut = *static_cast<CompoundBatch*>(batchOutPtr.get());
            batch.visitBatches([&templates, &batchOut, &toSort](auto const& columnKey,
                                                                auto const& genericColumn) {
              if(genericColumn.size() == 0) {
                return;
              }
              BatchHelperAny::visit(
                  // copy row values in sorted order, column per column
                  // TODO: insert together consecutive row values
                  // (or directly re-implement with a more efficient method...)
                  [&](auto const& column) {
                    using ColumnBatchType = std::decay_t<decltype(column)>;
                    auto newColumnBatchPtr = column.clone(true);
                    auto& newColumnBatch = *static_cast<ColumnBatchType*>(newColumnBatchPtr.get());
                    newColumnBatch.resize(column.size(), columnKey.first);
                    auto newColumnIt = newColumnBatch.begin();
                    for(auto& [key, rowIndex] : toSort) {
                      auto columnIt = column.begin() + rowIndex;
                      auto const& value = *columnIt;
                      if constexpr(std::is_base_of_v<BatchPtr, std::decay_t<decltype(value)>>) {
                        *newColumnIt = value->clone();
                      } else {
                        *newColumnIt = value;
                      }
                      ++newColumnIt;
                    }
                    batchOut.insert(columnKey.first, columnKey.second,
                                    std::move(newColumnBatchPtr));
                  },
                  genericColumn);
            });
            tableOut.insert(tableKey.first, tableKey.second, std::move(batchOutPtr));
          };

          tableView.visitBatches(
              [&sortFunction, &forEachBatchOfRows](auto const& tableKey, Batch const& batch) {
                if(batch.baseId() == UniqueId::forType<CompoundBatch>()) {
                  auto keysPtr = sortFunction.evaluateWith(std::vector<Batch const*>{&batch});
                  auto& keys = *keysPtr;
                  if(keys.size() == 0) {
                    return;
                  }
                  BatchHelperAny::visit(
                      [&forEachBatchOfRows, &tableKey, &batch](auto const& specificKeys) {
                        using KeyBatchType = std::decay_t<decltype(specificKeys)>;
                        if constexpr(!std::is_base_of_v<CompoundBatch, KeyBatchType>) {
                          forEachBatchOfRows(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                             specificKeys);
                        } else {
                          // TODO: how to do sorting if we handle list as a key?
                          // create a tuple of the values?
                        }
                      },
                      keys);
                }
              });
          return tableOutPtr;
        });
    templates.template argBatchTypes<TableView, FunctionBatch, FunctionBatch>()
        .template registerFunction<3>(
            "GroupBy",
            // groupFunction: Function(tuple) return a key
            // e.g to group by first column: "Function"_(List_("tuple"_), "Extract"_("tuple"_,
            // 1)) aggregator: Function("tuple", "aggregateResult") return the aggregate result
            // e.g to count: "Function"_(List_("tuple"_), "Count"_("Extract"_("tuple"_, 1)))
            // e.g to sum: "Function"_(List_("tuple"_), "Sum_("Extract"_("tuple"_, 1)))
            // e.g to return the key: "Function"_(List_("tuple"_), "Extract"_("tuple"_, 1))
            [&templates](auto const& tableView, auto const& groupFunction, auto const& aggregator) {
              auto tableOutPtr =
                  BatchPtr(new TableView(templates)); // not a clone so we clear columns too
              auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());

              auto forEachBatchOfRows = [&templates, &tableOut, &aggregator](
                                            auto const& tableKey, CompoundBatch const& batch,
                                            auto const& keys) {
                using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

                // create sorted indexes
                // TODO: any way to reserve?
                std::map<ElementType, std::vector<size_t>> sorted;
                size_t batchSize = batch.size();
                for(auto [keyIt, rowIndex] = std::pair{keys.begin(), size_t(0)};
                    rowIndex < batchSize; ++keyIt, ++rowIndex) {
                  sorted[*keyIt].push_back(rowIndex);
                }

                typename CompoundBatch::BatchList newBatches;
                newBatches.reserve(keys.size());

                auto groupedPtr = batch.clone(true);
                auto& grouped = *static_cast<CompoundBatch*>(groupedPtr.get());
                for(auto const& sortedIt : sorted) {
                  auto const& rowIndices = sortedIt.second;
                  batch.visitBatches([&templates, &grouped, &rowIndices](
                                         auto const& columnKey, auto const& genericColumn) {
                    if(genericColumn.size() == 0) {
                      return;
                    }
                    BatchHelperAny::visit(
                        // copy row values in sorted order, column per column
                        // TODO: insert together consecutive rows
                        // (or directly re-implement with a more efficient method...)
                        [&](auto const& column) {
                          using ColumnBatchType = std::decay_t<decltype(column)>;
                          auto newColumnBatchPtr = column.clone(true);
                          auto& newColumnBatch =
                              *static_cast<ColumnBatchType*>(newColumnBatchPtr.get());
                          newColumnBatch.resize(rowIndices.size(), columnKey.first);
                          auto newColumnIt = newColumnBatch.begin();
                          for(size_t rowIndex : rowIndices) {
                            auto columnIt = column.begin() + rowIndex;
                            auto const& value = *columnIt;
                            if constexpr(std::is_base_of_v<BatchPtr,
                                                           std::decay_t<decltype(value)>>) {
                              *newColumnIt = value->clone();
                            } else {
                              *newColumnIt = value;
                            }
                            ++newColumnIt;
                          }
                          grouped.insert(columnKey.first, columnKey.second,
                                         std::move(newColumnBatchPtr));
                        },
                        genericColumn);
                  });

                  // process to be called for each group of (sorted) table rows
                  auto aggregatedBatchPtr =
                      aggregator.evaluateWith(std::vector<Batch const*>{&grouped});
                  newBatches.emplace_back(std::move(aggregatedBatchPtr));
                  grouped.clear();
                }

                // TODO: set proper column names
                size_t needNumColumns = 0;
                if(!newBatches.empty()) {
                  if(newBatches.front()->baseId() == UniqueId::forType<CompoundBatch>()) {
                    auto const& compoundBatch =
                        *static_cast<CompoundBatch const*>(newBatches.front().get());
                    for(auto it = compoundBatch.begin(); it != compoundBatch.end(); ++it) {
                      tableOut.addColumn("aggr" + std::to_string(++needNumColumns));
                    }
                  } else {
                    needNumColumns = 1;
                    tableOut.addColumn("aggr");
                  }
                }

                for(auto& newBatchPtr : newBatches) {
                  tableOut.insert("List"_(templates.revertToExpression(*newBatchPtr)));
                }
              };

              tableView.visitBatches([&groupFunction, &forEachBatchOfRows](auto const& tableKey,
                                                                           Batch const& batch) {
                if(batch.baseId() == UniqueId::forType<CompoundBatch>()) {
                  auto keysPtr = groupFunction.evaluateWith(std::vector<Batch const*>{&batch});
                  auto& keys = *keysPtr;
                  if(keys.size() == 0) {
                    return;
                  }
                  BatchHelperAny::visit(
                      [&forEachBatchOfRows, &tableKey, &batch](auto const& specificKeys) {
                        using KeyBatchType = std::decay_t<decltype(specificKeys)>;
                        if constexpr(!std::is_base_of_v<CompoundBatch, KeyBatchType>) {
                          forEachBatchOfRows(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                             specificKeys);
                        } else {
                          // TODO: how to do sorting if we handle list as a key?
                          // create a tuple of the values?
                        }
                      },
                      keys);
                }
              });
              return tableOutPtr;
            });
  }

private:
  BatchTemplates& m_templates;

  // helpers to retrieve return type for a specific set of Batch argument types
  template <typename T> using FromBatchTypeToElementType = typename T::ValueType;
  template <typename T> struct BatchIsRLE { static constexpr auto value = T::IsRLE::value; };
  template <typename Func, typename... BatchTypes>
  using ReturnType = typename std::invoke_result_t<Func, FromBatchTypeToElementType<BatchTypes>...>;

  // helpers to iterate and evaluate on each element of a batch
  template <typename Func, typename... BatchIn>
  BatchPtr evaluateElements(Func&& func, BatchIn const&... in) {
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
      auto* outputBatch = new CompoundBatch(m_templates);
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
};

} // namespace boss::engines::bulk
