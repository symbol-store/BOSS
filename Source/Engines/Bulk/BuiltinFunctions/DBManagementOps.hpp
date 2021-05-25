#pragma once

#include "../OperatorUtils.hpp"

namespace boss::engines::bulk {

template <typename BatchPrototypes> class DBManagementOps {
  using Utils = OperatorUtils<BatchPrototypes>;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    manageTables(prototypes);
    manageColumns(prototypes);
    manageRows(prototypes);
  }

private:
  static void manageTables(BatchPrototypes& prototypes) {
    auto& tableViewPool = DefaultSymbolRegistry::instance();

    prototypes.template argTypes<Symbol>().template registerFunction<1>(
        "CreateTable", [&tableViewPool](auto&& batchPtr) {
          return Utils::evaluateElements(
              [&tableViewPool](auto const& table) -> Symbol {
                auto& symbolPtr = tableViewPool.findSymbol(table);
                symbolPtr = Batch::WritablePtr(new TableView());
                return table;
              },
              batchPtr);
        });

    prototypes.template argTypes<Symbol, Symbol>().template registerFunction<2>(
        "CreateTable", [&tableViewPool](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return Utils::evaluateElements(
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
          return Utils::evaluateElements(
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
          return Utils::evaluateElements(
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
};

} // namespace boss::engines::bulk
