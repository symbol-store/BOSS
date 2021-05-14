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

  // safe iterator which can fake iterating for arguments which are constants
  // it avois us to have to create a full size array for these constants
  template <typename It> class SafeIterator {
  public:
    explicit SafeIterator(It&& iterator, bool constant)
        : m_iterator(std::move(iterator)), m_constant(constant) {}
    auto operator*() { return *m_iterator; }
    bool operator!=(SafeIterator& rhs) { return m_iterator != rhs.m_iterator; }
    bool operator!=(SafeIterator&& rhs) { return m_iterator != rhs.m_iterator; }
    SafeIterator operator+(size_t incr) const {
      return SafeIterator(m_constant ? m_iterator : m_iterator + incr, m_constant);
    }
    void operator++() {
      if(!m_constant) {
        ++m_iterator;
      }
    }

  private:
    It m_iterator;
    bool m_constant;
  };

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
      auto* outputBatch = new ValueBatch<ReturnType>();
      outputBatch->resize(outputSize);
      apply(*outputBatch, in->begin()...);
      return Batch::WritablePtr(outputBatch);
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
          if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
            return evaluateElements(
                templates,
                [](auto const& a, auto const& b) -> bool {
                  return static_cast<ValueTypeA>(a) == static_cast<ValueTypeB>(b);
                },
                lhsBatchPtr, rhsBatchPtr);
          } else {
            return evaluateElements(
                templates, [](auto const& /*a*/, auto const& /*b*/) -> bool { return false; },
                lhsBatchPtr, rhsBatchPtr);
          }
        });
    templates.template allowedTypes<bool, int, float, std::string>().template registerFunction<2>(
        "NotEqual", [&templates](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
          using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
          using ValueTypeA = typename BatchTypeA::ValueType;
          using ValueTypeB = typename BatchTypeB::ValueType;
          if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
            return evaluateElements(
                templates,
                [](auto const& a, auto const& b) -> bool {
                  return static_cast<ValueTypeA>(a) != static_cast<ValueTypeB>(b);
                },
                lhsBatchPtr, rhsBatchPtr);
          } else {
            return evaluateElements(
                templates, [](auto const& /*a*/, auto const& /*b*/) -> bool { return true; },
                lhsBatchPtr, rhsBatchPtr);
          }
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

    templates.template argBatchTypes<AllowedBatches<CompoundBatch, SymbolBatch>, AnyBatch>()
        .template registerFunction<2>(
            "Function", [&templates](auto&& argBatchPtr, auto bodyBatchPtr) {
              using ArgsBatchPtrType = std::decay_t<decltype(argBatchPtr)>;
              using ArgsBatchType = typename ArgsBatchPtrType::BatchType;
              FunctionBatch::ParameterList args;
              if constexpr(std::is_base_of_v<CompoundBatch, ArgsBatchType>) {
                args.reserve(argBatchPtr->size());
                for(auto const& symbolBatchPtr : *argBatchPtr) {
                  if(symbolBatchPtr->typeId() == UniqueId::forType<SymbolBatch>()) {
                    args.emplace_back(
                        (*static_cast<SymbolBatch const*>(symbolBatchPtr.get())->begin()));
                  }
                }
              } else {
                args.emplace_back((*argBatchPtr->begin()));
              }
              Batch::ReadablePtr bodyPtr(std::move(bodyBatchPtr));
              return Batch::WritablePtr(new FunctionBatch(templates, args, std::move(bodyPtr)));
            });
  }

  static void collections(BatchTemplates& templates) {
    templates.template argBatchTypes<NonSymbolicBatch, ValueBatch<int>>()
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
                  return Batch::WritablePtr(templates.createBatch(value));
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
                std::vector<Batch::ReadablePtr> argBatches;
                argBatches.reserve(indexBatch.size());
                for(; indexIt != indexBatch.end() && exprIt != exprBatches.end();
                    ++indexIt, ++exprIt) {
                  size_t index = *indexIt - 1;
                  auto exprBatchPtr = *exprIt;
                  BatchHelperAny::visit(
                      [&index, &argBatches, &extraction](auto const& exprBatch) {
                        argBatches.emplace_back(extraction(exprBatch, index));
                      },
                      *exprBatchPtr);
                }
                compoundBatch.insert(argBatches);
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
            return Batch::WritablePtr(templates.createBatch(value));
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
            return Batch::WritablePtr(templates.createBatch(value));
          }
        });

    templates.template argBatchTypes<AllowedBatches<CompoundBatch, TableView>, ValueBatch<int>>()
        .template registerFunction<2>("Column", [](auto&& batchPtrExpr, auto&& batchPtrNth) {
          size_t index = *batchPtrNth->begin() - 1;
          return batchPtrExpr->column(index);
        });

    templates.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Length", [&templates](auto&& batchPtrExpr) {
          int value = batchPtrExpr->size();
          return Batch::WritablePtr(templates.createBatch(value));
        });
  }

  static void aggregates(BatchTemplates& templates) {
    templates.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Count", [&templates](auto&& batchPtr) {
          int value = batchPtr->size();
          return Batch::WritablePtr(templates.createBatch(value));
        });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Sum", [&templates](auto&& batchPtr) {
          auto it = batchPtr->begin();
          auto sum = *it;
          ++it;
          while(it != batchPtr->end()) {
            sum += *it;
            ++it;
          }
          return Batch::WritablePtr(templates.createBatch(sum));
        });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Min", [&templates](auto&& batchPtr) {
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
          return Batch::WritablePtr(templates.createBatch(min));
        });

    templates.template allowedTypes<int, float>().template registerFunction<1>(
        "Max", [&templates](auto&& batchPtr) {
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
          return Batch::WritablePtr(templates.createBatch(max));
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

    templates.template argTypes<Symbol, Symbol>().template registerFunction<2>(
        "CreateTable", [&templates, &tableViewPool](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return evaluateElements(
              templates,
              [&templates, &tableViewPool](auto const& table, auto const& column) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                TableView* tableView = nullptr;
                if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
                  tableView =
                      static_cast<TableView*>(Batch::WritablePtr::asWritable(symbolPtr).get());
                } else {
                  tableView = new TableView(templates);
                  symbolPtr = Batch::WritablePtr(tableView);
                }
                tableView->addColumn(column);
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

    templates.template argTypes<Symbol, Symbol>().template registerFunction<2>(
        "AddColumn", [&templates, &tableViewPool](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return evaluateElements(
              templates,
              [&tableViewPool](auto const& table, auto const& column) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
                  auto& tableView =
                      *static_cast<TableView*>(Batch::WritablePtr::asWritable(symbolPtr).get());
                  tableView.addColumn(column);
                }
                return table;
              },
              tableBatchPtr, columnBatchPtr);
        });

    templates.template argTypes<Symbol>().template registerFunction<1>(
        "Columns", [&tableViewPool](auto&& symbolBatchPtr) -> Batch::ReadablePtr {
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
                  Symbol missingSymbol("Missing");
                  size_t numColumns = tableView.numColumns();
                  size_t numRows =
                      rowBatchPtr->numArguments() > 0 ? (*rowBatchPtr->begin())->size() : 0;
                  static auto maximumRowFor1by1Insert = size_t(-1);
                  if(numRows <= maximumRowFor1by1Insert) {
                    // special case: insert directly from iterating on the source batch
                    if(tableView.numArguments() == 0) {
                      // but first insert empty columns, including missing columns
                      std::vector<Batch::ReadablePtr> argBatches;
                      argBatches.reserve(numColumns);
                      for(auto const& srcArgBatchPtr : *rowBatchPtr) {
                        argBatches.emplace_back(srcArgBatchPtr);
                      }
                      for(auto index = argBatches.size(); index < numColumns; ++index) {
                        auto missingBatchPtr =
                            Batch::WritablePtr(new SymbolBatch(templates, numRows, missingSymbol));
                        argBatches.emplace_back(std::move(missingBatchPtr));
                      }
                      tableView.initArguments(argBatches);
                    }
                    insertRowValues(tableView, *rowBatchPtr);
                    return;
                  }
                  std::vector<Batch::ReadablePtr> argBatches;
                  argBatches.reserve(numColumns);
                  // copy existing columns
                  for(auto batchPtr : *rowBatchPtr) {
                    argBatches.emplace_back(std::move(batchPtr));
                  }
                  // add missing columns
                  for(auto index = argBatches.size(); index < numColumns; ++index) {
                    auto missingBatchPtr =
                        Batch::WritablePtr(new SymbolBatch(templates, numRows, missingSymbol));
                    argBatches.emplace_back(std::move(missingBatchPtr));
                  }
                  tableView.insert(std::move(argBatches));
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
      auto tableOutPtr = tableViewPtr->template cloneAs<TableView>(true);
      auto& tableOut = *tableOutPtr;

      auto forEachBatchOfRows = [&tableOut](CompoundBatch const& batch, auto const& toKeep) {
        // TODO: optimisation if toKeep is all true or false?
        // all true => just transfer the pointer (no copy)
        // all false => nothing to do

        auto batchOutPtr = batch.cloneAsCompoundBatch(true);
        insertRowValuesWithCondition(*batchOutPtr, batch, toKeep);
        size_t numColumns = batchOutPtr->numArguments();
        if(numColumns > 0) {
          std::vector<Batch::ReadablePtr> argBatches;
          argBatches.reserve(numColumns);
          for(auto batchPtr : *batchOutPtr) {
            argBatches.emplace_back(std::move(batchPtr));
          }
          tableOut.insert(std::move(argBatches));
        }
      };

      // TODO: need to evaluate later more precisely
      // so only the rows actually used as criteria are evaluated
      // but for now it causes issues for where to set the "$tuple" information
      // (since the rows wouldn't be explicitely evaluated as a CBatch)
      Batch::ReadablePtr evaluatedRowsPtr;
      bool evaluated = tableViewPtr->evaluate(evaluatedRowsPtr);

      // evaluate the predicate
      std::vector<Batch::ReadablePtr> args;
      if(evaluated) {
        args.emplace_back(evaluatedRowsPtr);
      } else {
        args.emplace_back(tableViewPtr);
      }
      auto toKeepPtr = predicatePtr->evaluateWith(args);

      // apply the predicate
      BatchHelper<ValueBatch<bool>>::visit(
          [&evaluated, &tableViewPtr, &evaluatedRowsPtr, &forEachBatchOfRows](auto const& toKeep) {
            if(evaluated) {
              BatchHelper<CompoundBatch>::visit(
                  [&toKeep, &forEachBatchOfRows](auto const& batchofRows) {
                    forEachBatchOfRows(batchofRows, toKeep);
                  },
                  *evaluatedRowsPtr);
            } else {
              forEachBatchOfRows(*tableViewPtr, toKeep);
            }
          },
          *toKeepPtr);

      return Batch::WritablePtr(std::move(tableOutPtr));
    };

    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "Select", [select](auto&& tableViewPtr, auto&& predicatePtr) -> Batch::ReadablePtr {
          if(tableViewPtr->size() == 0) {
            return Batch::ReadablePtr(tableViewPtr);
          }
          return select(tableViewPtr, predicatePtr);
        });
  }

  static void projection(BatchTemplates& templates) {
    auto project = [&templates](auto&& tableViewPtr, auto&& projectorPtr) -> Batch::WritablePtr {
      WritableBatchPtr<TableView> tableOutPtr(
          new TableView(templates)); // not a clone so we clear columns too
      auto& tableOut = *tableOutPtr;

      // evaluate the projection
      std::vector<Batch::ReadablePtr> args;
      args.emplace_back(tableViewPtr);
      auto projectionPtr = projectorPtr->evaluateWith(args);

      // copy the new batches back to the table
      BatchHelper<CompoundBatch>::visit(
          [&tableOut](auto const& projectionBatch) {
            std::vector<Batch::ReadablePtr> columnBatches;
            for(auto srcBatchPtr : projectionBatch) {
              columnBatches.emplace_back(std::move(srcBatchPtr));
              // TODO: need to keep the column names
              // ideally, it should be part of the arrow storage
              tableOut.addColumn(Symbol("col" + std::to_string(tableOut.numColumns())));
            }
            tableOut.insert(columnBatches);
          },
          *projectionPtr);

      return Batch::WritablePtr(std::move(tableOutPtr));
    };

    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "Project", [project](auto&& tableViewPtr, auto&& projectorPtr) {
          return project(tableViewPtr, projectorPtr);
        });
  }

  static void sorting(BatchTemplates& templates) {
    // sortFunction: Function(tuple) return the key used for sorting
    // e.g to sort by first column: "Function"_(List_("tuple"_), "Column"_("tuple"_, 1))
    auto sortBy = [](auto&& tableViewPtr, auto&& sortFunctionPtr) -> Batch::WritablePtr {
      auto tableOutPtr = tableViewPtr->template cloneAs<TableView>(true);
      auto& tableOut = *tableOutPtr;

      auto forEachBatchOfRows = [&tableOut](CompoundBatch const& batch, auto& keys) {
        using ElementType = typename std::decay_t<decltype(keys)>::ValueType;

        size_t batchSize = batch.size();
        if(batchSize == 0) {
          return;
        }

        // create sorted indexes
        // TODO: any way to reserve?
        auto keyIt = keys.begin();
        std::map<ElementType, std::vector<size_t>> sorted;
        for(size_t rowIndex = 0; rowIndex < batchSize; ++rowIndex, ++keyIt) {
          sorted[*keyIt].push_back(rowIndex);
        }

        auto batchOutPtr = batch.cloneAsCompoundBatch(true);
        for(auto const& sortedIt : sorted) {
          auto const& rowIndices = sortedIt.second;
          insertRowValuesInOrder(*batchOutPtr, batch, rowIndices);
        }

        size_t numColumns = batchOutPtr->numArguments();
        if(numColumns > 0) {
          std::vector<Batch::ReadablePtr> argBatches;
          argBatches.reserve(numColumns);
          for(auto batchPtr : *batchOutPtr) {
            argBatches.emplace_back(std::move(batchPtr));
          }
          tableOut.insert(std::move(argBatches));
        }
      };

      // evaluate the keys
      std::vector<Batch::ReadablePtr> args;
      args.emplace_back(tableViewPtr);
      auto keysPtr = sortFunctionPtr->evaluateWith(args);

      // sort using these keys
      BatchHelperAny::visit(
          [&tableViewPtr, &forEachBatchOfRows](auto const& keys) {
            if(keys.size() == 0) {
              return;
            }
            using KeyBatchType = std::decay_t<decltype(keys)>;
            if constexpr(!std::is_base_of_v<CompoundBatch, KeyBatchType>) {
              forEachBatchOfRows(*tableViewPtr, keys);
            } else {
              // TODO: how to do sorting if we handle list as a key?
              // create a tuple of the values?
            }
          },
          *keysPtr);

      return Batch::WritablePtr(std::move(tableOutPtr));
    };

    templates.template argBatchTypes<TableView, FunctionBatch>().template registerFunction<2>(
        "SortBy", [sortBy](auto&& tableViewPtr, auto&& sortFunctionPtr) -> Batch::ReadablePtr {
          if(tableViewPtr->size() == 0) {
            return Batch::ReadablePtr(tableViewPtr);
          }
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
                                auto const& aggregator) -> Batch::WritablePtr {
      auto tableOutPtr = WritableBatchPtr<TableView>(
          new TableView(templates)); // not a clone so we clear columns too
      auto& tableOut = *tableOutPtr;

      auto aggregate = [&aggregator](auto& destbatch, auto const& srcBatch, auto const& sorted) {
        auto groupedPtr = srcBatch.cloneAsCompoundBatch(true);
        for(auto const& sortedIt : sorted) {
          // prepare the rows for the group to be processed
          auto const& rowIndices = sortedIt.second;
          insertRowValuesInOrder(*groupedPtr, srcBatch, rowIndices);

          // process to be called for each group of (sorted) table rows
          std::vector<Batch::ReadablePtr> args;
          args.emplace_back(groupedPtr);
          auto aggregatedBatchPtr = aggregator.evaluateWith(args);
          destbatch.emplace_back(std::move(aggregatedBatchPtr));
          groupedPtr->clear();
        }
      };

      auto CreateColumns = [&tableOut](auto const& batch) {
        // TODO: set proper column names
        for(size_t colIndex = tableOut.numColumns(); colIndex < batch.size(); ++colIndex) {
          tableOut.addColumn(Symbol("aggr" + std::to_string(++colIndex)));
        }
      };

      std::vector<Batch::WritablePtr> newBatches;

      auto forEachBatchOfRows = [&newBatches, &aggregate,
                                 &CreateColumns](CompoundBatch const& batch, auto const& keys) {
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

        auto const& firstBatch = *newBatches.front(); // any would do
        CreateColumns(firstBatch);
      };

      // evaluate the keys
      std::vector<Batch::ReadablePtr> args;
      args.emplace_back(tableViewPtr);
      auto keysPtr = groupFunctionPtr->evaluateWith(args);

      // sort using these keys
      BatchHelperAny::visit(
          [&tableViewPtr, &forEachBatchOfRows](auto const& keys) {
            if(keys.size() == 0) {
              return;
            }
            using KeyBatchType = std::decay_t<decltype(keys)>;
            if constexpr(!std::is_base_of_v<CompoundBatch, KeyBatchType>) {
              forEachBatchOfRows(*tableViewPtr, keys);
            } else {
              // TODO: how to do sorting if we handle list as a key?
              // create a tuple of the values?
            }
          },
          *keysPtr);

      tableOut.insert(std::vector<Batch::ReadablePtr>(newBatches.begin(), newBatches.end()));
      return Batch::WritablePtr(std::move(tableOutPtr));
    };

    templates
        .template argBatchTypes<TableView, FunctionBatch,
                                AllowedBatches<FunctionBatch, SymbolBatch>>()
        .template registerFunction<3>(
            "GroupBy",
            [groupBy, &templates](auto&& tableViewPtr, auto&& groupFunctionPtr,
                                  auto&& aggregatorPtr) -> Batch::ReadablePtr {
              if(tableViewPtr->size() == 0) {
                return Batch::ReadablePtr(tableViewPtr);
              }

              Batch::WritablePtr resultPtr;
              BatchHelper<FunctionBatch, SymbolBatch>::visit(
                  [&templates, &groupBy, &tableViewPtr, &groupFunctionPtr,
                   &resultPtr](auto const& aggregatorBatch) {
                    using BatchType = std::decay_t<decltype(aggregatorBatch)>;
                    if constexpr(std::is_same_v<BatchType, FunctionBatch>) {
                      resultPtr = groupBy(tableViewPtr, groupFunctionPtr, aggregatorBatch);
                    } else {
                      // construct an expression batch from the head (assuming single symbol value)
                      // also assuming a function with 1 argument only
                      Symbol const& head = *aggregatorBatch.begin();
                      Batch::WritablePtr bodyBatchPtr(templates.createBatch(head, 1));
                      // we pass a symbol as unique argument
                      Symbol functionArg("tuple");
                      bodyBatchPtr->insert(ComplexExpression(head, {functionArg}));
                      // and now we create a function batch using this expression as body
                      WritableBatchPtr<FunctionBatch> functionPtr(
                          new FunctionBatch(templates, FunctionBatch::ParameterList{functionArg},
                                            std::move(bodyBatchPtr)));
                      resultPtr = groupBy(tableViewPtr, groupFunctionPtr, *functionPtr);
                    }
                  },
                  *aggregatorPtr);
              return resultPtr;
            });
  }

  // helpers to retrieve return type for a specific set of Batch argument types
  template <typename T> using FromBatchTypeToElementType = typename T::ValueType;
  template <typename Func, typename... BatchPtrTypes>
  using ReturnType = typename std::invoke_result_t<
      Func, FromBatchTypeToElementType<typename BatchPtrTypes::BatchType>...>;

  template <typename DestBatchType, typename SrcBatchType>
  static void insertRowValuesInOrder(DestBatchType& destBatch, SrcBatchType const& srcBatch,
                                     std::vector<size_t> const& rowIndices) {
    // copy row values in sorted order, column per column
    // TODO: insert together consecutive rows
    // (or directly re-implement with a more efficient method...)
    if constexpr(!std::is_base_of_v<std::remove_const_t<SrcBatchType>, DestBatchType>) {
      using X = typename std::remove_const_t<SrcBatchType>::nothing;
      using Y = typename DestBatchType::nothing;
    }
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
      srcBatch.template visitBatches<BatchHelperAny>(
          [&childrenSize, &destArgBatchIt, &rowIndices](auto const& srcColumn) {
            using ColumnBatchType = std::decay_t<decltype(srcColumn)>;
            // insert to existing arg column
            auto destArgBatchPtr = *destArgBatchIt;
            BatchHelper<ColumnBatchType>::visit(
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

  template <typename DestBatchType, typename SrcBatchType>
  static void insertRowValues(DestBatchType& destBatch, SrcBatchType const& srcBatch) {
    // copy row values in sorted order, column per column
    // TODO: insert together consecutive rows
    // (or directly re-implement with a more efficient method...)
    if constexpr(!std::is_base_of_v<std::remove_const_t<SrcBatchType>, DestBatchType>) {
      using X = typename std::remove_const_t<SrcBatchType>::nothing;
      using Y = typename DestBatchType::nothing;
    }
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
      srcBatch.template visitBatches<BatchHelperAny>(
          [&childrenSize, &destArgBatchIt](auto const& srcColumn) {
            using ColumnBatchType = std::decay_t<decltype(srcColumn)>;
            // insert to existing arg column
            auto destArgBatchPtr = *destArgBatchIt;
            BatchHelper<ColumnBatchType>::visit(
                [&childrenSize, &srcColumn](auto& destColumn) {
                  insertRowValues(destColumn, srcColumn);
                  childrenSize = destColumn.size();
                },
                *Batch::WritablePtr::asWritable(std::move(destArgBatchPtr)));
            ++destArgBatchIt;
          });
      // and make sure to adjust the size of the parent array
      destBatch.resize(childrenSize);
    } else {
      size_t previousNumRows = destBatch.size();
      destBatch.resize(previousNumRows + srcBatch.size());
      auto srcBatchIt = srcBatch.begin();
      auto destBatchIt = destBatch.begin() + previousNumRows;
      for(; srcBatchIt != srcBatch.end(); ++srcBatchIt, ++destBatchIt) {
        *destBatchIt = *srcBatchIt;
      }
    }
  }

  template <typename DestBatchType, typename SrcBatchType, typename ConditionBatchType>
  static void insertRowValuesWithCondition(DestBatchType& destBatch, SrcBatchType const& srcBatch,
                                           ConditionBatchType const& conditionBatch) {
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
      srcBatch.template visitBatches<BatchHelperAny>(
          [&destArgBatchIt, &destArgBatchEnd, &conditionBatch, &argBatches](auto const& srcColumn) {
            using ColumnBatchType = std::decay_t<decltype(srcColumn)>;
            if(destArgBatchIt != destArgBatchEnd) {
              // insert to existing arg column
              auto& destArgBatchPtr = *destArgBatchIt;
              BatchHelper<ColumnBatchType>::visit(
                  [&srcColumn, &conditionBatch](auto& destColumn) {
                    insertRowValuesWithCondition(destColumn, srcColumn, conditionBatch);
                  },
                  *destArgBatchPtr);
              ++destArgBatchIt;
              return;
            }
            // create new arg column
            auto newColumnBatchPtr = srcColumn.template cloneAs<ColumnBatchType>(true);
            insertRowValuesWithCondition(*newColumnBatchPtr, srcColumn, conditionBatch);
            Batch::ReadablePtr toInsertPtr(std::move(newColumnBatchPtr));
            argBatches.emplace_back(std::move(toInsertPtr));
          });
      // if they are new arg columns, insert them now
      if(destBatch.numArguments() == 0) {
        if(!argBatches.empty() && argBatches[0]->size() > 0) {
          destBatch.insert(std::vector<Batch::ReadablePtr>(argBatches.begin(), argBatches.end()));
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
};

} // namespace boss::engines::bulk
