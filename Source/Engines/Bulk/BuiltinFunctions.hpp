#pragma once

#include "Batch/FunctionBatch.hpp"
#include "BatchTemplates.hpp"
#include "SymbolPool.hpp"
#include "TableView.hpp"

#include "../../Expression.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace boss::engines::bulk {

/****************** class BuiltinFunctions ********************/

/* Helper class just for registering all the builin functions */
/**************************************************************/

template <typename... SupportedTypes> class BuiltinFunctions {
public:
  using BatchTemplates = BatchTemplates<SupportedTypes...>;
  using CompoundBatch = typename BatchTemplates::CompoundBatch;
  using FunctionBatch = typename BatchTemplates::FunctionBatch;
  using AnyExpressionBatch = typename BatchTemplates::AnyExpressionBatch;
  using TableView = typename BatchTemplates::TableView;

  BuiltinFunctions(BatchTemplates& templates) {
    auto& symbolPool = DefaultSymbolPool::instance();
    auto& tableViewPool = WritableBatchPool::instance();

    // Arithmetic
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Plus", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Minus", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Times", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Divide", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatch, rhsBatch);
        });

    // Comparison
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Equal", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a == b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "NotEqual", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a != b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Less", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a < b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "LessEqual", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a <= b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Greater", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a > b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "GreaterEqual", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a >= b; }, lhsBatch, rhsBatch);
        });

    // Logic
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "And", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a && b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "Or", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a || b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool>().template registerFunction<1>(
        "Not", [](auto const& batch) {
          return BuiltinFunctions::evaluateElements([](auto const& a) -> bool { return !a; },
                                                    batch);
        });

    // Strings
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringJoin", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> std::string { return a + b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringContainsQ", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
              lhsBatch, rhsBatch);
        });

    // Symbolic
    templates.template allowedTypes<std::string>().template registerFunction<1>(
        "Symbol", [](auto const& batch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& name) -> Symbol { return Symbol(name); }, batch);
        });
    templates.template argTypes<Symbol, ComplexExpression>().template registerFunction<2>(
        "Set", [&symbolPool, &templates](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [&symbolPool, &templates](auto const& symbol, auto const& expr) -> Symbol {
                auto batchPtr = templates.createBatch(expr);
                batchPtr.get()->insert(expr);

                symbolPool.findSymbol(symbol) = std::move(batchPtr);
                return symbol;
              },
              lhsBatch, rhsBatch);
        });
    templates.template argBatchTypes<CompoundBatch, AnyExpressionBatch>()
        .template registerFunction<2>("Function", [](auto const& argBatch, auto const& definition) {
          std::vector<Symbol> args;
          args.reserve(argBatch.numBatches());
          for(size_t index = 0; index < args.capacity(); ++index) {
            auto symbolBatchPtr = argBatch.extract(index);
            if(symbolBatchPtr.get()->typeId() == UniqueId::forType<SymbolBatch>()) {
              args.emplace_back(*static_cast<SymbolBatch*>(symbolBatchPtr.get())->begin());
            }
          }
          return BatchPtr(new FunctionBatch(args, definition));
        });
    // temporarly needed until Function/Set can support any batch type as definition
    templates.template allowedTypes<bool, int, float, std::string>().template registerFunction<1>(
        "Constant", [](auto const& batch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a) -> auto { return a; }, batch);
        });

    // Collections
    templates.template argBatchTypes<CompoundBatch, RLEBatch<int>>().template registerFunction<2>(
        "Extract", [](auto const& batchExpr, auto const& batchNth) {
          // assuming batchNth is a fixed value along the rows...
          return batchExpr.extract(*batchNth.begin() - 1);
        });
    templates.template argBatchTypes<CompoundBatch>().template registerFunction<1>(
        "Length", [](auto const& batchExpr) {
          // TODO: need to review the interface for batches
          // it shouldn't be dependent on how the batches are organising the inner batches
          // (ideally should call a generic length() function not numBatches())
          // also there is a function size() a bit misleading since it is the size for batching
          // itself (so batch implementation dependent too)
          return BatchPtr(new RLEBatch<int>(1, batchExpr.size()));
          // return *(new RLEBatch<int>(1, batchExpr.numBatches()));
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
        "CreateTable", [&templates, &tableViewPool](auto const& batch) {
          return BuiltinFunctions::evaluateElements(
              [&templates, &tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr = WritableBatchPool::SymbolPtr(new TableView(templates));
                return table;
              },
              batch);
        });
    templates.template argTypes<Symbol>().template registerFunction<1>(
        "RemoveTable", [&templates, &tableViewPool](auto const& batch) {
          return BuiltinFunctions::evaluateElements(
              [&templates, &tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr.reset();
                return table;
              },
              batch);
        });
    templates.template argBatchTypes<SymbolBatch, RLEBatch<std::string>>()
        .template registerFunction<2>("AddColumn", [&templates,
                                                    &tableViewPool](auto const& tableBatch,
                                                                    auto const& columnBatch) {
          return BuiltinFunctions::evaluateElements(
              [&templates, &tableViewPool](auto const& table, auto const& columnName) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                if(symbolPtr && symbolPtr.get()->typeId() == UniqueId::forType<TableView>()) {
                  auto& tableView = *static_cast<TableView*>(symbolPtr.get());
                  tableView.addColumn(columnName);
                }
                return table;
              },
              tableBatch, columnBatch);
        });
    templates.template argTypes<Symbol, ComplexExpression>().template registerFunction<2>(
        "InsertInto", [&templates, &tableViewPool](auto const& tableBatch, auto const& rowBatch) {
          return BuiltinFunctions::evaluateElements(
              [&templates, &tableViewPool](auto const& table, auto const& row) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                if(symbolPtr && symbolPtr.get()->typeId() == UniqueId::forType<TableView>()) {
                  auto& tableView = *symbolPtr.get();
                  tableView.insert(row);
                }
                return table;
              },
              tableBatch, rowBatch);
        });
    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "Select", [](auto const& tableView, auto const& predicate) {
          auto tableOutPtr = tableView.clone(true);
          auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());
          // process to be called for each batch of table rows
          auto forEachBatch = [&tableOut](auto const& tableKey, CompoundBatch const& batch,
                                          auto const& toKeep) {
            auto batchOutPtr = batch.clone(true);
            auto& batchOut = *static_cast<CompoundBatch*>(batchOutPtr.get());
            size_t columnIndex = 0;
            batch.visitBatches([&batchOut, &toKeep, &columnIndex](auto const& column) {
              auto columnIt = column.begin();
              auto toKeepIt = toKeep.begin();
              for(; columnIt != column.end() && toKeepIt != toKeep.end(); ++columnIt, ++toKeepIt) {
                if(*toKeepIt) {
                  batchOut.insert(columnIndex, *columnIt);
                }
              }
              ++columnIndex;
            });
            tableOut.insert(tableKey, std::move(batchOutPtr));
          };

          tableView.visitBatches(
              [&predicate, &forEachBatch](auto const& tableKey, Batch const& batch) {
                if(batch.typeId() == UniqueId::forType<CompoundBatch>()) {
                  auto toKeepPtr = predicate.evaluateWith(std::vector<Batch const*>{&batch});
                  auto& toKeep = *toKeepPtr.get();
                  if(toKeep.typeId() == UniqueId::forType<ValueBatch<bool>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<ValueBatch<bool>*>(&toKeep));
                  } else if(toKeep.typeId() == UniqueId::forType<RLEBatch<bool>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<RLEBatch<bool>*>(&toKeep));
                  }
                }
              });

          return tableOutPtr;
        });

    templates.template argBatchTypes<TableView, CompoundBatch>().template registerFunction<2>(
        "Project", [&templates](auto const& tableView, auto const& columns) {
          auto tableOutPtr = BatchPtr(new TableView(templates));
          auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());
          std::vector<size_t> indexes;
          indexes.reserve(columns.numBatches());
          for(size_t i = 0; i < indexes.capacity(); ++i) {
            auto columnBatchPtr = columns.extract(i);
            auto& columnBatch = *columnBatchPtr.get();
            int index = -1;
            if(columnBatch.typeId() == UniqueId::forType<ValueBatch<std::string>>()) {
              auto const& columnName =
                  *static_cast<ValueBatch<std::string>*>(&columnBatch)->begin();
              index = tableView.columnIndex(columnName);
              if(index >= 0) {
                indexes.push_back(index);
                tableOut.addColumn(columnName);
              }
            } else if(columnBatch.typeId() == UniqueId::forType<RLEBatch<std::string>>()) {
              auto const& columnName = *static_cast<RLEBatch<std::string>*>(&columnBatch)->begin();
              index = tableView.columnIndex(columnName);
              if(index >= 0) {
                indexes.push_back(index);
                tableOut.addColumn(columnName);
              }
            }
          }
          if(!indexes.empty()) {
            tableView.visitBatches([&indexes, &tableOut](auto const& tableKey, Batch const& batch) {
              if(batch.typeId() == UniqueId::forType<CompoundBatch>()) {
                auto const& compoundBatch = *static_cast<CompoundBatch const*>(&batch);
                // for each batch of rows
                typename CompoundBatch::BatchList columnBatches;
                columnBatches.reserve(indexes.size());
                // check which columns we want to keep
                for(size_t index : indexes) {
                  // copy specific column
                  auto columnBatchPtr = compoundBatch.extract(static_cast<size_t>(index));
                  columnBatches.emplace_back(std::move(columnBatchPtr));
                }

                auto rowBatchPtr = BatchPtr(new CompoundBatch("List"_, std::move(columnBatches)));
                tableOut.insert(tableKey, std::move(rowBatchPtr));
              }
            });
          }
          return tableOutPtr;
        });
    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "SortBy",
        // sortFunction: Function(tuple) return the key used for sorting
        // e.g to sort by first column: "Function"_(List_("tuple"_), "Extract"_("tuple"_, 1))
        [](auto const& tableView, auto const& sortFunction) {
          auto tableOutPtr = tableView.clone(true);
          auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());
          // process to be called for each batch of table rows
          auto forEachBatch = [&tableOut](auto const& tableKey, CompoundBatch const& batch,
                                          auto& keys) {
            using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

            // create sorted indexes
            std::vector<std::pair<ElementType, size_t>> toSort;
            toSort.reserve(batch.size());
            for(auto [keyIt, rowIndex] = std::pair{keys.begin(), size_t(0)};
                keyIt != keys.end() && rowIndex < toSort.capacity(); ++keyIt, ++rowIndex) {
              toSort.push_back(std::make_pair(*keyIt, rowIndex));
            }
            std::sort(toSort.begin(), toSort.end(),
                      [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });

            // copy rows in sorted order, column per column
            // TODO: insert together consecutive rows
            // (or re-implement more efficient method...)
            auto batchOutPtr = batch.clone(true);
            auto& batchOut = *static_cast<CompoundBatch*>(batchOutPtr.get());
            for(auto& [key, rowIndex] : toSort) {
              size_t columnIndex = 0;
              batch.visitBatches(
                  [&batchOut, rowIndex = rowIndex, &columnIndex](auto const& column) {
                    auto rowIt = column.begin() + rowIndex;
                    batchOut.insert(columnIndex, *rowIt);
                    ++columnIndex;
                  });
            }

            tableOut.insert(tableKey, std::move(batchOutPtr));
          };

          tableView.visitBatches(
              [&sortFunction, &forEachBatch](auto const& tableKey, Batch const& batch) {
                if(batch.typeId() == UniqueId::forType<CompoundBatch>()) {
                  auto keysPtr = sortFunction.evaluateWith(std::vector<Batch const*>{&batch});
                  auto& keys = *keysPtr.get();
                  if(keys.size() == 0) {
                    return;
                  }
                  // TODO: refactor this
                  if(keys.typeId() == UniqueId::forType<ValueBatch<bool>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<ValueBatch<bool>*>(&keys));
                  } else if(keys.typeId() == UniqueId::forType<RLEBatch<bool>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<RLEBatch<bool>*>(&keys));
                  } else if(keys.typeId() == UniqueId::forType<ValueBatch<int>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<ValueBatch<int>*>(&keys));
                  } else if(keys.typeId() == UniqueId::forType<RLEBatch<int>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<RLEBatch<int>*>(&keys));
                  } else if(keys.typeId() == UniqueId::forType<ValueBatch<float>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<ValueBatch<float>*>(&keys));
                  } else if(keys.typeId() == UniqueId::forType<RLEBatch<float>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<RLEBatch<float>*>(&keys));
                  } else if(keys.typeId() == UniqueId::forType<ValueBatch<std::string>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<ValueBatch<std::string>*>(&keys));
                  } else if(keys.typeId() == UniqueId::forType<RLEBatch<std::string>>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<RLEBatch<std::string>*>(&keys));
                  } else if(keys.typeId() == UniqueId::forType<SymbolBatch>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<SymbolBatch*>(&keys));
                    // TODO: how to do sorting if we handle list as a key?
                    // create a tuple of the values?
                  } /*else if(keys.typeId() == UniqueId::forType<CompoundBatch>()) {
                    forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                 *static_cast<CompoundBatch*>(&keys));
                  }*/
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
              auto tableOutPtr = tableView.clone(true);
              auto& tableOut = *static_cast<TableView*>(tableOutPtr.get());
              // process to be called for each batch of table rows
              auto forEachBatch = [&tableOut, &aggregator](auto const& tableKey,
                                                           CompoundBatch const& batch,
                                                           auto const& keys) {
                using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

                // create sorted indexes
                std::vector<std::pair<ElementType, size_t>> toSort;
                toSort.reserve(batch.size());
                for(auto [keyIt, rowIndex] = std::pair{keys.begin(), size_t(0)};
                    keyIt != keys.end() && rowIndex < toSort.capacity(); ++keyIt, ++rowIndex) {
                  toSort.push_back(std::make_pair(*keyIt, rowIndex));
                }
                std::sort(toSort.begin(), toSort.end(),
                          [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });

                typename CompoundBatch::BatchList newBatches;
                newBatches.reserve(keys.size());

                // process to be called for each group of (sorted) table rows
                auto aggregate = [&aggregator, &newBatches](auto& groupedBatch) {
                  auto aggregatedBatchPtr =
                      aggregator.evaluateWith(std::vector<Batch const*>{&groupedBatch});
                  newBatches.emplace_back(std::move(aggregatedBatchPtr));
                };

                auto groupedPtr = batch.clone(true);
                auto& grouped = *static_cast<CompoundBatch*>(groupedPtr.get());
                auto prevKey = toSort.front().first;
                auto startRowIndex = 0;
                for(auto& [key, rowIndex] : toSort) {
                  // if we finished with a group, apply aggregate and insert
                  if(key != prevKey) {
                    aggregate(grouped);
                    grouped.clear();
                  }
                  prevKey = key;

                  // copy rows in sorted order, column per column
                  // TODO: insert together consecutive rows
                  // (or re-implement more efficient method...)
                  size_t columnIndex = 0;
                  batch.visitBatches(
                      [&grouped, rowIndex = rowIndex, &columnIndex](auto const& column) {
                        auto rowIt = column.begin() + rowIndex;
                        grouped.insert(columnIndex, *rowIt);
                        ++columnIndex;
                      });
                }
                // and handle last group too
                if(grouped.size() > 0) {
                  aggregate(grouped);
                }

                size_t needNumColumns = 1;
                if(newBatches.size() > 0 &&
                   newBatches.front().get()->typeId() == UniqueId::forType<CompoundBatch>()) {
                  auto& compoundBatch =
                      *static_cast<CompoundBatch const*>(newBatches.front().get());
                  needNumColumns = compoundBatch.numBatches();
                }
                for(size_t i = tableOut.numColumns(); i < needNumColumns; ++i) {
                  // TODO: set proper column names
                  tableOut.addColumn("aggr" + std::to_string(i));
                }

                auto rowBatchPtr = BatchPtr(new CompoundBatch("List"_, std::move(newBatches)));
                tableOut.insert(tableKey, std::move(rowBatchPtr));
              };

              tableView.visitBatches(
                  [&groupFunction, &forEachBatch](auto const& tableKey, Batch const& batch) {
                    if(batch.typeId() == UniqueId::forType<CompoundBatch>()) {
                      auto keysPtr = groupFunction.evaluateWith(std::vector<Batch const*>{&batch});
                      auto& keys = *keysPtr.get();
                      if(keys.size() == 0) {
                        return;
                      }
                      // TODO: refactor this
                      if(keys.typeId() == UniqueId::forType<ValueBatch<bool>>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<ValueBatch<bool>*>(&keys));
                      } else if(keys.typeId() == UniqueId::forType<RLEBatch<bool>>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<RLEBatch<bool>*>(&keys));
                      } else if(keys.typeId() == UniqueId::forType<ValueBatch<int>>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<ValueBatch<int>*>(&keys));
                      } else if(keys.typeId() == UniqueId::forType<RLEBatch<int>>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<RLEBatch<int>*>(&keys));
                      } else if(keys.typeId() == UniqueId::forType<ValueBatch<float>>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<ValueBatch<float>*>(&keys));
                      } else if(keys.typeId() == UniqueId::forType<RLEBatch<float>>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<RLEBatch<float>*>(&keys));
                      } else if(keys.typeId() == UniqueId::forType<ValueBatch<std::string>>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<ValueBatch<std::string>*>(&keys));
                      } else if(keys.typeId() == UniqueId::forType<RLEBatch<std::string>>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<RLEBatch<std::string>*>(&keys));
                      } else if(keys.typeId() == UniqueId::forType<SymbolBatch>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<SymbolBatch*>(&keys));
                        // TODO: how to do sorting if we handle list as a key?
                        // create a tuple of the values?
                      } /*else if(keys.typeId() == UniqueId::forType<CompoundBatch>()) {
                        forEachBatch(tableKey, *static_cast<CompoundBatch const*>(&batch),
                                     *static_cast<CompoundBatch*>(&keys));
                      }*/
                    }
                  });
              return tableOutPtr;
            });
  }

private:
  // helpers to retrieve return type for a specific set of Batch argument types
  template <typename T> using FromBatchTypeToElementType = typename T::ValueType;
  template <typename T> struct BatchIsRLE { static constexpr auto value = T::IsRLE::value; };
  template <typename Func, typename... BatchTypes>
  using ReturnType = typename std::invoke_result_t<Func, FromBatchTypeToElementType<BatchTypes>...>;

  // helpers to iterate and evaluate on each element of a batch
  template <typename Func, typename... BatchIn>
  static BatchPtr evaluateElements(Func&& func, BatchIn const&... in) {
    auto apply = [&](auto& out, auto&&... inIt) {
      auto outIt = out.begin();
      for(; outIt != out.end() && ((inIt != in.end()) && ...); ++outIt, ((++inIt), ...)) {
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
      auto* outputBatch = new CompoundBatch();
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
