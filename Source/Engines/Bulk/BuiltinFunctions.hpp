#pragma once

#include "../Bulk.hpp"
#include "Batch/FunctionBatch.hpp"
#include "Batch/TableView.hpp"
#include "BatchPrototypes.hpp"
#include "SymbolRegistry.hpp"

#include "../../Expression.hpp"
#include "../../Utilities.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace boss::engines::bulk {

using boss::utilities::operator""_;

/****************** class BuiltinFunctions ********************/

/* Helper class just for registering all the builin functions */
/**************************************************************/

template <typename BatchPrototypes> class BuiltinFunctions {
  // Wow, this thing is massive. I think we want to spend some time refactoring it (happy to do that
  // in a pair). In particular, I think we should see what utility functions we want to
  // extract. Extending the engine should be really (!) easy
public:
  using AnyBatch = typename BatchPrototypes::AnyBatch;
  using NonSymbolicBatch = typename BatchPrototypes::NonSymbolicBatch;
  using AnySimpleBatch = typename BatchPrototypes::AnySimpleBatch;
  using AnyCompoundBatch = typename BatchPrototypes::AnyCompoundBatch;

  static void registerAll(BatchPrototypes& prototypes) {
    arithmetic(prototypes);
    comparison(prototypes);
    logic(prototypes);
    conversions(prototypes);
    stringOps(prototypes);
    symbolicOps(prototypes);
    collections(prototypes);
    aggregates(prototypes);
    dbManagement(prototypes);
    queries(prototypes);
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
  static Batch::WritablePtr evaluateElements(Func&& func, BatchPtrIn&&... in) {
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
      apply(*outputBatch, in->begin()...);
      return Batch::WritablePtr(outputBatch);
    } else if constexpr(std::is_same_v<ReturnType, ComplexExpression>) {
      auto* outputBatch = new CompoundBatch();
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
  static void arithmetic(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Plus", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Minus", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Times", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Divide", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<1>(
        "Negation", [](auto&& lhsBatchPtr) {
          return evaluateElements(
              [](auto const& a) -> auto { return -a; }, lhsBatchPtr);
        });
    prototypes.template allowedTypes<float>().template registerFunction<3>(
        "Lerp", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr, auto&& ratioBatchPtr) {
          return evaluateElements(

              [](auto const& a, auto const& b, auto const& t) -> auto { return a + (b - a) * t; },
              lhsBatchPtr, rhsBatchPtr, ratioBatchPtr);
        });
  };

  static void comparison(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<bool, int, float, std::string>().template registerFunction<2>(
        "Equal", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
          using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
          using ValueTypeA = typename BatchTypeA::ValueType;
          using ValueTypeB = typename BatchTypeB::ValueType;
          if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
            return evaluateElements(
                [](auto const& a, auto const& b) -> bool {
                  return static_cast<ValueTypeA>(a) == static_cast<ValueTypeB>(b);
                },
                lhsBatchPtr, rhsBatchPtr);
          } else {
            return evaluateElements(
                [](auto const& /*a*/, auto const& /*b*/) -> bool { return false; }, lhsBatchPtr,
                rhsBatchPtr);
          }
        });
    prototypes.template allowedTypes<bool, int, float, std::string>().template registerFunction<2>(
        "NotEqual", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
          using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
          using ValueTypeA = typename BatchTypeA::ValueType;
          using ValueTypeB = typename BatchTypeB::ValueType;
          if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
            return evaluateElements(
                [](auto const& a, auto const& b) -> bool {
                  return static_cast<ValueTypeA>(a) != static_cast<ValueTypeB>(b);
                },
                lhsBatchPtr, rhsBatchPtr);
          } else {
            return evaluateElements(
                [](auto const& /*a*/, auto const& /*b*/) -> bool { return true; }, lhsBatchPtr,
                rhsBatchPtr);
          }
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Less", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a < b; },
                                  lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "LessEqual", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a <= b; },
                                  lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Greater", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a > b; },
                                  lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "GreaterEqual", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a >= b; },
                                  lhsBatchPtr, rhsBatchPtr);
        });
  }

  static void logic(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<bool>().template registerFunction<2>(
        "And", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a && b; },
                                  lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<bool>().template registerFunction<2>(
        "Or", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements([](auto const& a, auto const& b) -> bool { return a || b; },
                                  lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<bool>().template registerFunction<1>(
        "Not", [](auto const& batch) {
          return evaluateElements([](auto const& a) -> bool { return !a; }, batch);
        });
  }

  static void conversions(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<std::string>().template registerFunction<1>(
        "UnixTime", [](auto const& batch) {
          return evaluateElements(
              [](auto const& str) -> int {
                std::istringstream iss;
                iss.str(str);
                struct std::tm tm = {};
                iss >> std::get_time(&tm, "%Y-%m-%d");
                int value = std::mktime(&tm);
                return value;
              },
              batch);
        });
  }

  static void stringOps(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<std::string>().template registerFunction<2>(
        "StringJoin", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements([](auto const& a, auto const& b) -> std::string { return a + b; },
                                  lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<std::string>().template registerFunction<2>(
        "StringContainsQ", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
              lhsBatchPtr, rhsBatchPtr);
        });
  }

  static void symbolicOps(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<std::string>().template registerFunction<1>(
        "Symbol", [](auto&& batchPtr) {
          return evaluateElements([](auto const& name) -> Symbol { return Symbol(name); },
                                  batchPtr);
        });

    prototypes.template argBatchTypes<AnySimpleBatch>().template registerFunction<1>(
        "Function", [](auto bodyBatchPtr) {
          Batch::ReadablePtr bodyPtr(std::move(bodyBatchPtr));
          return Batch::WritablePtr(
              new FunctionBatch(FunctionBatch::ParameterList{}, std::move(bodyPtr)));
        });

    prototypes.template argBatchTypes<AllowedBatches<CompoundBatch, SymbolBatch>, AnyBatch>()
        .template registerFunction<2>("Function", [](auto&& argBatchPtr, auto bodyBatchPtr) {
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
          return Batch::WritablePtr(new FunctionBatch(args, std::move(bodyPtr)));
        });
  }

  static void collections(BatchPrototypes& prototypes) {
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

  static void aggregates(BatchPrototypes& prototypes) {
    prototypes.template argBatchTypes<NonSymbolicBatch>().template registerFunction<1>(
        "Count", [](auto&& batchPtr) {
          int value = batchPtr->size();
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(value));
        });

    prototypes.template allowedTypes<int, float>().template registerFunction<1>(
        "Sum", [](auto&& batchPtr) {
          auto it = batchPtr->begin();
          auto sum = *it;
          ++it;
          while(it != batchPtr->end()) {
            sum += *it;
            ++it;
          }
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(sum));
        });

    prototypes.template allowedTypes<int, float>().template registerFunction<1>(
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
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(min));
        });

    prototypes.template allowedTypes<int, float>().template registerFunction<1>(
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
          return Batch::WritablePtr(Engine::getBatchFactory().createBatch(max));
        });
  }

  static void dbManagement(BatchPrototypes& prototypes) {
    manageTables(prototypes);
    manageColumns(prototypes);
    manageRows(prototypes);
  }

  static void manageTables(BatchPrototypes& prototypes) {
    auto& tableViewPool = DefaultSymbolRegistry::instance();

    prototypes.template argTypes<Symbol>().template registerFunction<1>(
        "CreateTable", [&tableViewPool](auto&& batchPtr) {
          return evaluateElements(
              [&tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr = Batch::WritablePtr(new TableView());
                return table;
              },
              batchPtr);
        });

    prototypes.template argTypes<Symbol, Symbol>().template registerFunction<2>(
        "CreateTable", [&tableViewPool](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return evaluateElements(
              [&tableViewPool](auto const& table, auto const& column) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                TableView* tableView = nullptr;
                if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
                  tableView =
                      static_cast<TableView*>(Batch::WritablePtr::asWritable(symbolPtr).get());
                } else {
                  tableView = new TableView();
                  symbolPtr = Batch::WritablePtr(tableView);
                }
                tableView->addColumn(column);
                return table;
              },
              tableBatchPtr, columnBatchPtr);
        });

    prototypes.template argTypes<Symbol>().template registerFunction<1>(
        "RemoveTable", [&tableViewPool](auto&& batchPtr) {
          return evaluateElements(
              [&tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr.reset();
                return table;
              },
              batchPtr);
        });
  }

  static void manageColumns(BatchPrototypes& prototypes) {
    auto& tableViewPool = DefaultSymbolRegistry::instance();

    prototypes.template argTypes<Symbol, Symbol>().template registerFunction<2>(
        "AddColumn", [&tableViewPool](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return evaluateElements(
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

    prototypes.template argTypes<Symbol>().template registerFunction<1>(
        "Columns", [&tableViewPool](auto&& symbolBatchPtr) -> Batch::ReadablePtr {
          auto& symbolPtr = tableViewPool.findSymbol(*symbolBatchPtr->begin());
          if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
            auto const& tableView = *static_cast<TableView const*>(symbolPtr.get());
            return Batch::ReadablePtr(tableView.columns());
          }
          return Batch::WritablePtr(new ValueBatch<std::string>());
        });
  }

  static void manageRows(BatchPrototypes& prototypes) {
    auto& tableViewPool = DefaultSymbolRegistry::instance();

    prototypes.template argTypes<Symbol, ComplexExpression>().template registerFunction<2>(
        "InsertInto", [&tableViewPool](auto symbolBatchPtr, auto rowBatchPtr) {
          auto& symbolPtr = tableViewPool.findSymbol(*symbolBatchPtr->begin());
          if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<TableView>()) {
            BatchVisitDispatcher<TableView>::visit(
                [&rowBatchPtr](auto& tableView) {
                  size_t numColumns = tableView.numColumns();
                  size_t numArgsToInsert = rowBatchPtr->numArguments();
                  size_t numRowsToInsert =
                      numArgsToInsert > 0 ? (*rowBatchPtr->begin())->size() : 0;
                  std::vector<Batch::ReadablePtr> argBatches;
                  argBatches.reserve(numColumns);
                  // copy existing columns
                  for(auto batchPtr : *rowBatchPtr) {
                    argBatches.emplace_back(std::move(batchPtr));
                  }
                  // add missing columns
                  Symbol missingSymbol("Missing");
                  for(auto index = argBatches.size(); index < numColumns; ++index) {
                    auto missingBatchPtr =
                        Batch::WritablePtr(new SymbolBatch(numRowsToInsert, missingSymbol));
                    argBatches.emplace_back(std::move(missingBatchPtr));
                  }
                  tableView.append(std::move(argBatches));
                },
                *Batch::WritablePtr::asWritable(symbolPtr));
          }
          return Batch::WritablePtr::asWritable(Batch::ReadablePtr(symbolBatchPtr));
        });
  }

  static void queries(BatchPrototypes& prototypes) {
    selection(prototypes);
    projection(prototypes);
    sorting(prototypes);
    grouping(prototypes);
  }

  static void selection(BatchPrototypes& prototypes) {
    auto select = [](auto&& tableViewPtr, auto&& predicatePtr) -> Batch::WritablePtr {
      auto& tableOut = *tableViewPtr->template cloneAs<TableView>(true);

      auto forEachBatchOfRows = [&tableOut](CompoundBatch const& batch, auto const& toKeep) {
        // TODO: optimisation if toKeep is all true or false?
        // all true => just transfer the pointer (no copy)
        // all false => nothing to do

        auto batchOutPtr = WritableBatchPtr(batch.cloneAsCompoundBatch(true));
        BuiltinFunctions::insertRowValuesWithCondition(*batchOutPtr, batch, toKeep);
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
          BuiltinFunctions::insertRowValuesInOrder(*batchOutPtr, batch, rowIndices);
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
    auto groupBy = [](auto&& tableViewPtr, auto&& groupFunctionPtr,
                      auto const& aggregator) -> Batch::WritablePtr {
      auto& tableOut = *(new TableView()); // not a clone so we clear columns too

      auto aggregate = [&aggregator](auto& destbatches, auto const& srcBatch, auto const& sorted) {
        auto groupedPtr = WritableBatchPtr(srcBatch.cloneAsCompoundBatch(true));
        for(auto const& sortedIt : sorted) {
          // prepare the rows for the group to be processed
          auto const& rowIndices = sortedIt.second;
          BuiltinFunctions::insertRowValuesInOrder(*groupedPtr, srcBatch, rowIndices);

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
            "GroupBy",
            [&prototypes, groupBy](auto&& tableViewPtr, auto&& groupFunctionPtr,
                                   auto&& aggregatorPtr) -> Batch::ReadablePtr {
              Batch::WritablePtr resultPtr;
              BatchVisitDispatcher<FunctionBatch, SymbolBatch>::visit(
                  [&prototypes, &groupBy, &tableViewPtr, &groupFunctionPtr,
                   &resultPtr](auto const& aggregatorBatch) {
                    using BatchType = std::decay_t<decltype(aggregatorBatch)>;
                    if constexpr(std::is_same_v<BatchType, FunctionBatch>) {
                      resultPtr = groupBy(tableViewPtr, groupFunctionPtr, aggregatorBatch);
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

  /// copy row values in sorted order (based on indices), column per column
  template <typename DestBatchType, typename SrcBatchType>
  static void insertRowValuesInOrder(DestBatchType& destBatch, SrcBatchType const& srcBatch,
                                     std::vector<size_t> const& rowIndices) {
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
      srcBatch.template visitBatches<typename BatchPrototypes::BatchVisitDispatcher>(
          [&childrenSize, &destArgBatchIt, &rowIndices](auto const& srcColumn) {
            using ColumnBatchType = std::decay_t<decltype(srcColumn)>;
            // insert to existing arg column
            auto destArgBatchPtr = *destArgBatchIt;
            BatchVisitDispatcher<ColumnBatchType>::visit(
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

  /// copy row values if matches a condition, column per column
  template <typename DestBatchType, typename SrcBatchType, typename ConditionBatchType>
  static void insertRowValuesWithCondition(DestBatchType& destBatch, SrcBatchType const& srcBatch,
                                           ConditionBatchType const& conditionBatch) {
    static_assert(std::is_base_of_v<std::remove_const_t<SrcBatchType>, DestBatchType>);
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
      srcBatch.template visitBatches<typename BatchPrototypes::BatchVisitDispatcher>(
          [&destArgBatchIt, &destArgBatchEnd, &conditionBatch, &argBatches](auto const& srcColumn) {
            using ColumnBatchType = std::decay_t<decltype(srcColumn)>;
            if(destArgBatchIt != destArgBatchEnd) {
              // insert to existing arg column
              auto& destArgBatchPtr = *destArgBatchIt;
              BatchVisitDispatcher<ColumnBatchType>::visit(
                  [&srcColumn, &conditionBatch](auto& destColumn) {
                    insertRowValuesWithCondition(destColumn, srcColumn, conditionBatch);
                  },
                  *destArgBatchPtr);
              ++destArgBatchIt;
              return;
            }
            // create new arg column
            auto newColumnBatchPtr =
                WritableBatchPtr(srcColumn.template cloneAs<ColumnBatchType>(true));
            insertRowValuesWithCondition(*newColumnBatchPtr, srcColumn, conditionBatch);
            Batch::ReadablePtr toInsertPtr(std::move(newColumnBatchPtr));
            argBatches.emplace_back(std::move(toInsertPtr));
          });
      // if they are new arg columns, insert them now
      if(destBatch.numArguments() == 0) {
        if(!argBatches.empty() && argBatches[0]->size() > 0) {
          destBatch.append(std::vector<Batch::ReadablePtr>(argBatches.begin(), argBatches.end()));
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
