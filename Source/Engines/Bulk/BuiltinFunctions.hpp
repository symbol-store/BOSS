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
  using NonSymbolicBatch = typename BatchTemplates::NonSymbolicBatch;
  using AnySimpleBatch = typename BatchTemplates::AnySimpleBatch;
  using AnyCompoundBatch = typename BatchTemplates::AnyCompoundBatch;
  using BatchHelperAny = typename BatchTemplates::BatchHelper;

  static void registerAll(BatchTemplates& templates) {
    arithmetic(templates);
    comparison(templates);
    logic(templates);
    stringOps(templates);
    symbolicOps(templates);
    collections(templates);
    aggregates(templates);
    dbManagement(templates);
    queries(templates);
  }

  // helpers to iterate and evaluate on each element of a batch
  template <typename Func, typename... BatchPtrIn>
  static Batch::WritablePtr evaluateElements(BatchTemplates& templates, Func&& func,
                                             BatchPtrIn&&... in) {
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
      auto* outputBatch = new SymbolBatch(templates, 1);
      apply(*outputBatch, in->begin()...);
      return Batch::WritablePtr(outputBatch);
    } else if constexpr(std::is_same_v<ReturnType, ComplexExpression>) {
      auto* outputBatch = new CompoundBatch(templates);
      apply(*outputBatch, in->begin()...);
      return Batch::WritablePtr(outputBatch);
    } else {
      size_t outputSize = 1;
      (..., [&outputSize, &in]() { outputSize = std::max(outputSize, in->size()); }());
      if constexpr((... && BatchPtrIsRLE<std::decay_t<decltype(in)>>::value)) {
        auto* outputBatch = new RLEBatch<ReturnType>(outputSize, ReturnType());
        apply(*outputBatch, in->begin()...);
        return Batch::WritablePtr(outputBatch);
      } else {
        auto* outputBatch = new ValueBatch<ReturnType>(outputSize, ReturnType());
        apply(*outputBatch, in->begin()...);
        return Batch::WritablePtr(outputBatch);
      }
    }
  }

private:
  static void arithmetic(BatchTemplates& templates) {
    templates.template allowedTypes<int, float>().template registerFunction<2>(
        "Plus", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<int, float>().template registerFunction<2>(
        "Minus", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<int, float>().template registerFunction<2>(
        "Times", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<int, float>().template registerFunction<2>(
        "Divide", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Negation", [&templates](auto&& lhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a) -> auto { return -a; }, lhsBatchPtr);
        });
    templates.template allowedTypes<float>().template registerFunction<3>(
        "Lerp", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr, auto&& ratioBatchPtr) {
          return evaluateElements(
              templates,
              [](auto const& a, auto const& b, auto const& t) -> auto { return a + (b - a) * t; },
              lhsBatchPtr, rhsBatchPtr, ratioBatchPtr);
        });
  };

  static void comparison(BatchTemplates& templates) {
    templates.template allowedTypes<bool, int, float, std::string>().template registerFunction<2>(
        "Equal", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
          using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
          using ValueTypeA = typename BatchTypeA::ValueType;
          using ValueTypeB = typename BatchTypeB::ValueType;
          return evaluateElements(
              templates,
              []([[maybe_unused]] auto const& a, [[maybe_unused]] auto const& b) -> bool {
                if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
                  return static_cast<ValueTypeA>(a) == static_cast<ValueTypeB>(b);
                } else {
                  return false;
                }
              },
              lhsBatchPtr, rhsBatchPtr);
        });
    templates.template allowedTypes<bool, int, float, std::string>().template registerFunction<2>(
        "NotEqual", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
          using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
          using ValueTypeA = typename BatchTypeA::ValueType;
          using ValueTypeB = typename BatchTypeB::ValueType;
          return evaluateElements(
              templates,
              []([[maybe_unused]] auto const& a, [[maybe_unused]] auto const& b) -> bool {
                if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
                  return static_cast<ValueTypeA>(a) != static_cast<ValueTypeB>(b);
                } else {
                  return true;
                }
              },
              lhsBatchPtr, rhsBatchPtr);
        });
    templates.template allowedTypes<int, float>().template registerFunction<2>(
        "Less", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a < b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<int, float>().template registerFunction<2>(
        "LessEqual", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a <= b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<int, float>().template registerFunction<2>(
        "Greater", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a > b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<int, float>().template registerFunction<2>(
        "GreaterEqual", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a >= b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
  }

  static void logic(BatchTemplates& templates) {
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "And", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a && b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "Or", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> bool { return a || b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    templates.template allowedTypes<bool>().template registerFunction<1>(
        "Not", [&templates](auto const& batch) {
          return evaluateElements(
              templates, [](auto const& a) -> bool { return !a; }, batch);
        });
  }

  static void stringOps(BatchTemplates& templates) {
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringJoin", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates, [](auto const& a, auto const& b) -> std::string { return a + b; },
              lhsBatchPtr, rhsBatchPtr);
        });
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringContainsQ", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              templates,
              [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
              lhsBatchPtr, rhsBatchPtr);
        });
  }

  static void symbolicOps(BatchTemplates& templates) {
    auto& symbolPool = DefaultSymbolPool::instance();

    templates.template allowedTypes<std::string>().template registerFunction<1>(
        "Symbol", [&templates](auto&& batchPtr) {
          return evaluateElements(
              templates, [](auto const& name) -> Symbol { return Symbol(name); }, batchPtr);
        });

    templates.template argBatchTypes<AnySimpleBatch>().template registerFunction<1>(
        "Function", [&templates](auto bodyBatchPtr) {
          Batch::ReadablePtr bodyPtr(std::move(bodyBatchPtr));
          return Batch::WritablePtr(
              new FunctionBatch(templates, FunctionBatch::ParameterList{}, std::move(bodyPtr)));
        });

    templates.template argBatchTypes<CompoundBatch, AnyExpressionBatch>()
        .template registerFunction<2>("Function", [&templates](auto&& argBatchPtr,
                                                               auto bodyBatchPtr) {
          FunctionBatch::ParameterList args;
          args.reserve(argBatchPtr->size());
          for(auto const& symbolBatchPtr : *argBatchPtr) {
            if(symbolBatchPtr->typeId() == UniqueId::forType<SymbolBatch>()) {
              args.emplace_back(*static_cast<SymbolBatch const*>(symbolBatchPtr.get())->begin());
            }
          }
          Batch::ReadablePtr bodyPtr(std::move(bodyBatchPtr));
          return Batch::WritablePtr(new FunctionBatch(templates, args, std::move(bodyPtr)));
        });
  }

  static void collections(BatchTemplates& templates) {
    templates
        .template argBatchTypes<NonSymbolicBatch, AllowedBatches<ValueBatch<int>, RLEBatch<int>>>()
        .template registerFunction<2>(
            "Extract",
            [&templates](auto&& exprBatchesPtr, auto&& indexBatchPtr) -> Batch::ReadablePtr {
              auto extraction = [&templates](auto const& exprBatch, size_t index) {
                using BatchType = std::decay_t<decltype(exprBatch)>;
                using ValueType = typename BatchType::ValueType;
                if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
                  return exprBatch.extract(index);
                } else {
                  auto const& value = static_cast<ValueType>(*(exprBatch.begin() + index));
                  auto batchPtr = templates.createBatch(value);
                  batchPtr->insert(value);
                  return batchPtr;
                }
              };

              auto const& exprBatches = *exprBatchesPtr;
              auto const& indexBatch = *indexBatchPtr;
              if(indexBatch.size() == 1) {
                // special case for single batch extraction
                size_t index = *indexBatch.begin() - 1;
                return Batch::ReadablePtr(extraction(exprBatches, index));
              }
              // general case for multiple extraction
              // exprBatchesPtr should be a compound
              using BatchType = std::decay_t<decltype(exprBatches)>;
              auto compoundBatchPtr = exprBatches.template cloneAs<BatchType>(true);
              if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
                auto& compoundBatch = *compoundBatchPtr;
                auto indexIt = indexBatch.begin();
                auto exprIt = exprBatches.begin();
                size_t newIndex = 0;
                for(; indexIt != indexBatch.end() && exprIt != exprBatches.end();
                    ++indexIt, ++exprIt) {
                  size_t index = *indexIt - 1;
                  auto exprBatchPtr = *exprIt;
                  BatchHelperAny::visit(
                      [&templates, &index, &newIndex, &compoundBatch,
                       &extraction](auto const& exprBatch) {
                        auto batchPtr = extraction(exprBatch, index);
                        compoundBatch.insert(templates.toKey(*batchPtr), newIndex++,
                                             std::move(batchPtr));
                      },
                      *exprBatchPtr);
                }
              }
              return Batch::ReadablePtr(std::move(compoundBatchPtr));
            });

    templates.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "First", [&templates](auto&& batchPtrExpr) {
          using BatchPtrType = std::decay_t<decltype(batchPtrExpr)>;
          using BatchType = typename BatchPtrType::BatchType;
          using ValueType = typename BatchType::ValueType;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            return batchPtrExpr->extract(0);
          } else {
            auto const& value = static_cast<ValueType>(*batchPtrExpr->begin());
            auto batchPtr = templates.createBatch(value);
            batchPtr->insert(value);
            return batchPtr;
          }
        });

    templates.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Last", [&templates](auto&& batchPtrExpr) {
          using BatchPtrType = std::decay_t<decltype(batchPtrExpr)>;
          using BatchType = typename BatchPtrType::BatchType;
          using ValueType = typename BatchType::ValueType;
          size_t index = batchPtrExpr->size() - 1;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            return batchPtrExpr->extract(index);
          } else {
            auto const& value = static_cast<ValueType>(*(batchPtrExpr->begin() + index));
            auto batchPtr = templates.createBatch(value);
            batchPtr->insert(value);
            return batchPtr;
          }
        });

    templates
        .template argBatchTypes<AllowedBatches<CompoundBatch, TableView>,
                                AllowedBatches<ValueBatch<int>, RLEBatch<int>>>()
        .template registerFunction<2>("Column", [](auto&& batchPtrExpr, auto&& batchPtrNth) {
          size_t index = *batchPtrNth->begin() - 1;
          return batchPtrExpr->reduce(index);
        });

    templates.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Length", [](auto&& batchPtrExpr) {
          return Batch::WritablePtr(new RLEBatch<int>(1, batchPtrExpr->size()));
        });
  }

  static void aggregates(BatchTemplates& templates) {
    templates.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Count",
        [](auto&& batchPtr) { return Batch::WritablePtr(new RLEBatch<int>(1, batchPtr->size())); });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Sum", [](auto&& batchPtr) {
          auto it = batchPtr->begin();
          auto sum = *it;
          ++it;
          while(it != batchPtr->end()) {
            sum += *it;
            ++it;
          }
          return Batch::WritablePtr(new RLEBatch<decltype(sum)>(1, sum));
        });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Min", [](auto&& batchPtr) {
          auto it = batchPtr->begin();
          auto min = *it;
          ++it;
          while(it != batchPtr->end()) {
            auto value = *it;
            if(value < min) {
              min = value;
            }
            ++it;
          }
          return Batch::WritablePtr(new RLEBatch<decltype(min)>(1, min));
        });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Max", [](auto&& batchPtr) {
          auto it = batchPtr->begin();
          auto max = *it;
          ++it;
          while(it != batchPtr->end()) {
            auto value = *it;
            if(value > max) {
              max = value;
            }
            ++it;
          }
          return Batch::WritablePtr(new RLEBatch<decltype(max)>(1, max));
        });
  }

  static void dbManagement(BatchTemplates& templates) {
    manageTables(templates);
    manageColumns(templates);
    manageRows(templates);
  }

  static void manageTables(BatchTemplates& templates) {
    auto& tableViewPool = DefaultSymbolPool::instance();

    templates.template argTypes<Symbol>().template registerFunction<1>(
        "CreateTable", [&templates, &tableViewPool](auto&& batchPtr) {
          return evaluateElements(
              templates,
              [&templates, &tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr = Batch::WritablePtr(new TableView(templates));
                return table;
              },
              batchPtr);
        });

    templates.template argTypes<Symbol, std::string>().template registerFunction<2>(
        "CreateTable", [&templates, &tableViewPool](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return evaluateElements(
              templates,
              [&templates, &tableViewPool](auto const& table, auto const& columnName) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                auto* tableView = new TableView(templates);
                if(!symbolPtr || symbolPtr->typeId() != UniqueId::forType<TableView>()) {
                  symbolPtr = Batch::WritablePtr(tableView);
                }
                tableView->addColumn(columnName);
                return table;
              },
              tableBatchPtr, columnBatchPtr);
        });

    templates.template argTypes<Symbol>().template registerFunction<1>(
        "RemoveTable", [&templates, &tableViewPool](auto&& batchPtr) {
          return evaluateElements(
              templates,
              [&tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr.reset();
                return table;
              },
              batchPtr);
        });
  }

  static void manageColumns(BatchTemplates& templates) {
    auto& tableViewPool = DefaultSymbolPool::instance();

    templates.template argTypes<Symbol, std::string>().template registerFunction<2>(
        "AddColumn", [&templates, &tableViewPool](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return evaluateElements(
              templates,
              [&tableViewPool](auto const& table, auto const& columnName) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
                  auto& tableView =
                      *static_cast<TableView*>(Batch::WritablePtr::asWritable(symbolPtr).get());
                  tableView.addColumn(columnName);
                }
                return table;
              },
              tableBatchPtr, columnBatchPtr);
        });

    templates.template argTypes<Symbol>().template registerFunction<1>(
        "Columns", [&templates, &tableViewPool](auto&& symbolBatchPtr) -> Batch::ReadablePtr {
          auto& symbolPtr = tableViewPool.findSymbol(*symbolBatchPtr->begin());
          if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
            auto const& tableView = *static_cast<TableView const*>(symbolPtr.get());
            return Batch::ReadablePtr(tableView.columns());
          }
          return Batch::WritablePtr(new ValueBatch<std::string>());
        });
  }

  static void manageRows(BatchTemplates& templates) {
    auto& tableViewPool = DefaultSymbolPool::instance();

    templates.template argTypes<Symbol, ComplexExpression>().template registerFunction<2>(
        "InsertInto", [&templates, &tableViewPool](auto symbolBatchPtr, auto rowBatchPtr) {
          auto& symbolPtr = tableViewPool.findSymbol(*symbolBatchPtr->begin());
          if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
            BatchHelper<TableView>::visit(
                [&templates, &rowBatchPtr](auto& tableView) {
                  ExpressionArguments args;
                  args.reserve(tableView.numColumns());
                  rowBatchPtr->visitBatches(
                      [&args](auto const& columnKey, auto const& /*columnPtr*/) {
                        args.emplace_back(columnKey.first);
                      });
                  auto numColumns = tableView.numColumns();
                  auto rowSize = args.size();
                  if(rowSize < numColumns) {
                    auto writableBatchPtr =
                        WritableBatchPtr<CompoundBatch>::asWritable(std::move(rowBatchPtr));
                    Symbol missingSymbol("Missing");
                    for(auto index = rowSize; index < numColumns; ++index) {
                      args.emplace_back(missingSymbol);
                      auto missingBatchPtr =
                          Batch::WritablePtr(new SymbolBatch(templates, 1, missingSymbol));
                      writableBatchPtr->insert(missingSymbol, index, std::move(missingBatchPtr));
                    }
                    rowBatchPtr = std::move(writableBatchPtr);
                  }
                  Batch::ReadablePtr toInsertPtr(std::move(rowBatchPtr));
                  tableView.insert(ComplexExpression(Symbol("List"), std::move(args)), 0,
                                   std::move(toInsertPtr));
                },
                *Batch::WritablePtr::asWritable(symbolPtr));
          }
          Batch::ReadablePtr outputPtr(symbolBatchPtr);
          return Batch::WritablePtr::asWritable(std::move(outputPtr));
        });
  }

  static void queries(BatchTemplates& templates) {
    selection(templates);
    projection(templates);
    sorting(templates);
    grouping(templates);
  }

  static void selection(BatchTemplates& templates) {
    auto select = [](auto&& tableViewPtr, auto&& predicatePtr) -> Batch::WritablePtr {
      auto tableOutPtr = WritableBatchPtr<TableView>::asWritable(tableViewPtr, true);
      auto& tableOut = *tableOutPtr;

      auto forEachBatchOfRows = [&tableOut](auto const& tableKey, CompoundBatch const& batch,
                                            auto const& toKeep) {
        // TODO: optimisation if toKeep is RLE
        // all true => just transfer the pointer (no copy)
        // all false => nothing to do
        auto batchOutPtr = batch.cloneAsCompoundBatch(true);
        auto& batchOut = *batchOutPtr;

        batch.visitBatches<BatchHelperAny>(
            [&toKeep, &batchOut](auto const& columnKey, auto const& column) {
              using ColumnBatchType = std::decay_t<decltype(column)>;
              auto newColumnBatchPtr = column.template cloneAs<ColumnBatchType>(true);
              auto& newColumnBatch = *newColumnBatchPtr;
              size_t newColumnSize =
                  copyRowValuesWithCondition(newColumnBatch, column, toKeep, columnKey.first);
              if(newColumnSize == 0) {
                return;
              }
              Batch::ReadablePtr toInsertPtr(std::move(newColumnBatchPtr));
              batchOut.insert(columnKey.first, columnKey.second, std::move(toInsertPtr));
            });

        if(batchOut.size() > 0) {
          Batch::ReadablePtr toInsertPtr(std::move(batchOutPtr));
          tableOut.insert(tableKey.first, tableKey.second, std::move(toInsertPtr));
        }
      };

      tableViewPtr->template visitBatches([&predicatePtr, &forEachBatchOfRows](
                                              auto const& tableKey, auto const& batchOfRowsPtr) {
        // TODO: need to evaluate later more precisely
        // so only the rows actually used as criteria are evaluated
        // but for now it causes issues for where to set the "$tuple" information
        // (since the rows wouldn't be explicitely evaluated as a CBatch)
        Batch::ReadablePtr evaluatedRowsPtr;
        bool evaluated = batchOfRowsPtr->evaluate(evaluatedRowsPtr);
        BatchHelper<CompoundBatch>::visit(
            [&predicatePtr, &forEachBatchOfRows, &tableKey](auto const& batchofRows) {
              auto toKeepPtr = predicatePtr->evaluateWith(std::vector<Batch const*>{&batchofRows});
              auto const& toKeep = *toKeepPtr;
              BatchHelper<ValueBatch<bool>, RLEBatch<bool>>::visit(
                  [&](auto const& toKeepAsBool) {
                    forEachBatchOfRows(tableKey, batchofRows, toKeepAsBool);
                  },
                  toKeep);
            },
            evaluated ? *evaluatedRowsPtr : *batchOfRowsPtr);
      });

      return Batch::WritablePtr(std::move(tableOutPtr));
    };

    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "Select", [select](auto&& tableViewPtr, auto&& predicatePtr) {
          return select(tableViewPtr, predicatePtr);
        });
  }

  static void projection(BatchTemplates& templates) {
    auto project = [&templates](auto&& tableViewPtr, auto&& columnsPtr) -> Batch::WritablePtr {
      WritableBatchPtr<TableView> tableOutPtr(
          new TableView(templates)); // not a clone so we clear columns too
      auto& tableOut = *tableOutPtr;

      // fill the indexes
      std::vector<size_t> indexes;
      indexes.reserve(columnsPtr->size());
      // TODO: handle columns as a batch of string
      // but wouldn't work until we load an homogenous list as value/rle batch
      for(auto& columnBatchPtr : *columnsPtr) {
        Batch::ReadablePtr columnNamePtr = std::move(columnBatchPtr);
        BatchHelper<SymbolBatch>::visit(
            [&columnNamePtr](auto& symbolBatch) { symbolBatch.evaluate(columnNamePtr); },
            *columnNamePtr);
        if(!columnNamePtr) {
          continue;
        }
        BatchHelper<ValueBatch<std::string>, RLEBatch<std::string>>::visit(
            [&indexes, &tableViewPtr, &tableOut](auto& columnBatch) {
              auto const& columnName = *columnBatch.begin();
              int index = tableViewPtr->columnIndex(columnName);
              if(index < 0) {
                return;
              }
              indexes.push_back(index);
              tableOut.addColumn(columnName);
            },
            *columnNamePtr);
      }

      // copy the new batches from the indexes
      if(!indexes.empty()) {
        tableViewPtr->template visitBatches<BatchHelper<CompoundBatch>>(
            [&templates, &indexes, &tableOut](auto const& tableKey, auto const& oldColumns) {
              auto newColumnsPtr = oldColumns.cloneAsCompoundBatch(true);
              auto& newColumns = *newColumnsPtr;
              auto const& columnKeyExpression = std::get<ComplexExpression>(tableKey.first);
              auto const& columnKeyArgs = columnKeyExpression.getArguments();
              size_t newIndex = 0;
              for(size_t index : indexes) {
                auto columnBatchPtr = (*(oldColumns.begin() + index));
                auto& columnKeyArg = columnKeyArgs[index];
                newColumns.insert(columnKeyArg, newIndex++, std::move(columnBatchPtr));
              }
              auto const& key = templates.toKey(*newColumnsPtr);
              Batch::ReadablePtr toInsertPtr(std::move(newColumnsPtr));
              tableOut.insert(key, tableKey.second, std::move(toInsertPtr));
            });
      }
      return Batch::WritablePtr(std::move(tableOutPtr));
    };

    templates.template argBatchTypes<TableView, CompoundBatch>().template registerFunction<2>(
        "Project", [project](auto&& tableViewPtr, auto&& columnsPtr) {
          return project(tableViewPtr, columnsPtr);
        });
  }

  static void sorting(BatchTemplates& templates) {
    // sortFunction: Function(tuple) return the key used for sorting
    // e.g to sort by first column: "Function"_(List_("tuple"_), "Column"_("tuple"_, 1))
    auto sortBy = [](auto&& tableViewPtr, auto&& sortFunctionPtr) -> Batch::WritablePtr {
      auto tableOutPtr = WritableBatchPtr<TableView>::asWritable(tableViewPtr, true);
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
                // TODO: handle CBatch (see select)
                copyRowValuesInOrder(newcolumnIt, newcolumnItEnd, column, rowIndices);
              }
              batchOut.insert(columnKey.first, columnKey.second, std::move(newColumnBatchPtr));
            });
        Batch::ReadablePtr toInsertPtr(std::move(batchOutPtr));
        tableOut.insert(tableKey.first, tableKey.second, std::move(toInsertPtr));
      };

      tableViewPtr->template visitBatches<BatchHelper<CompoundBatch>>(
          [&sortFunctionPtr, &forEachBatchOfRows](auto const& tableKey, auto const& batchOfRows) {
            auto keysPtr = sortFunctionPtr->evaluateWith(std::vector<Batch const*>{&batchOfRows});
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
      return Batch::WritablePtr(std::move(tableOutPtr));
    };

    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "SortBy", [sortBy](auto&& tableViewPtr, auto&& sortFunctionPtr) {
          return sortBy(tableViewPtr, sortFunctionPtr);
        });
  }

  static void grouping(BatchTemplates& templates) {
    // groupFunction: Function(tuple) return a key
    // e.g to group by first column: "Function"_(List_("tuple"_), "Extract"_("tuple"_,
    // 1)) aggregator: Function("tuple", "aggregateResult") return the aggregate result
    // e.g to count: "Function"_(List_("tuple"_), "Count"_("Extract"_("tuple"_, 1)))
    // e.g to sum: "Function"_(List_("tuple"_), "Sum_("Extract"_("tuple"_, 1)))
    // e.g to return the key: "Function"_(List_("tuple"_), "Extract"_("tuple"_, 1))
    auto groupBy = [&templates](auto&& tableViewPtr, auto&& groupFunctionPtr,
                                auto&& aggregatorPtr) -> Batch::WritablePtr {
      auto tableOutPtr = WritableBatchPtr<TableView>(
          new TableView(templates)); // not a clone so we clear columns too
      auto& tableOut = *tableOutPtr;

      auto aggregate = [&aggregatorPtr](auto& destbatch, auto const& srcBatch, auto const& sorted) {
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
                // TODO: handle CBatch (see select)
                copyRowValuesInOrder(newColumnBatchIt, newColumnBatchItEnd, column, rowIndices);
                grouped.insert(columnKey.first, columnKey.second, std::move(newColumnBatchPtr));
              });

          // process to be called for each group of (sorted) table rows
          auto aggregatedBatchPtr =
              aggregatorPtr->evaluateWith(std::vector<Batch const*>{&grouped});
          destbatch.emplace_back(std::move(aggregatedBatchPtr));
          grouped.clear();
        }
      };

      auto InsertRows = [&tableOut](auto const& batch) {
        // TODO: set proper column names
        for(size_t colIndex = tableOut.numColumns(); colIndex < batch.size(); ++colIndex) {
          tableOut.addColumn("aggr" + std::to_string(++colIndex));
        }
      };

      std::vector<Batch::WritablePtr> newBatches;
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

      tableViewPtr->template visitBatches<BatchHelper<CompoundBatch>>(
          [&groupFunctionPtr, &forEachBatchOfRows](auto const& tableKey, auto const& batchOfRows) {
            auto keysPtr = groupFunctionPtr->evaluateWith(std::vector<Batch const*>{&batchOfRows});
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

      int rowIndex = 0;
      for(auto& newBatchPtr : newBatches) {
        auto const& key = templates.toKey(*newBatchPtr);
        tableOut.insert(key, rowIndex++, std::move(newBatchPtr));
      }
      return Batch::WritablePtr(std::move(tableOutPtr));
    };

    templates.template argBatchTypes<TableView, FunctionBatch, FunctionBatch>()
        .template registerFunction<3>(
            "GroupBy",
            [groupBy](auto&& tableViewPtr, auto&& groupFunctionPtr, auto&& aggregatorPtr) {
              return groupBy(tableViewPtr, groupFunctionPtr, aggregatorPtr);
            });
  }

  // helpers to retrieve return type for a specific set of Batch argument types
  template <typename T> using FromBatchTypeToElementType = typename T::ValueType;
  template <typename BatchPtrType> struct BatchPtrIsRLE {
    static constexpr auto value = BatchPtrType::BatchType::IsRLE::value;
  };
  template <typename Func, typename... BatchPtrTypes>
  using ReturnType = typename std::invoke_result_t<
      Func, FromBatchTypeToElementType<typename BatchPtrTypes::BatchType>...>;

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
      auto srcBatchIt = srcBatch.begin() + *rowIndexIt;
      *destBatchIt = *srcBatchIt;
    }
  }

  template <typename DestBatchType, typename SrcBatchType, typename ConditionBatchType>
  static size_t copyRowValuesWithCondition(DestBatchType& destBatch, SrcBatchType const& srcBatch,
                                           ConditionBatchType const& conditionBatch,
                                           Expression const& key) {
    size_t numRows = 0;
    if constexpr(std::is_base_of_v<CompoundBatch, SrcBatchType>) {
      auto const& args = std::get<ComplexExpression>(key).getArguments();
      size_t srcSize = srcBatch.size();
      auto conditionIt = conditionBatch.begin();
      for(size_t i = 0; i < srcSize; ++i, ++conditionIt) {
        if(!*conditionIt) {
          continue;
        }
        auto exprPtr = srcBatch.extract(i);
        destBatch.insert(args[i], numRows++, std::move(exprPtr));
      }
    } else {
      destBatch.resize(srcBatch.size(), key); // pessimistic
      auto srcBatchIt = srcBatch.begin();
      auto conditionIt = conditionBatch.begin();
      auto destBatchIt = destBatch.begin();
      for(; srcBatchIt != srcBatch.end() && destBatchIt != destBatch.end();
          ++srcBatchIt, ++conditionIt) {
        if(!*conditionIt) {
          continue;
        }
        *destBatchIt = *srcBatchIt;
        ++destBatchIt;
        ++numRows;
      }
      destBatch.resize(numRows, key); // shrink it back
    }
    return numRows;
  }
};

} // namespace boss::engines::bulk
