#pragma once

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class DBManagementOps {

public:
  static void registerAll() {
    manageTables();
    manageColumns();
    manageRows();
  }

private:
  static void manageTables() {
    auto& symbolRegistry = DefaultSymbolRegistry::instance();
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template argTypes<Symbol>().template registerFunction<1>(
        "CreateTable", [&symbolRegistry](auto&& batchPtr) {
          return OperatorUtils::evaluateElements(
              [&symbolRegistry](auto const& table) -> Symbol {
                auto& symbolPtr = symbolRegistry.findSymbol(table);
                symbolPtr = Batch::WritablePtr(new CompoundBatch(true));
                return table;
              },
              batchPtr);
        });

    operatorRegistry.template argTypes<Symbol, Symbol>().template registerFunction<2>(
        "CreateTable", [&symbolRegistry](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return OperatorUtils::evaluateElements(
              [&symbolRegistry](auto const& table, auto const& column) -> Symbol {
                auto& symbolPtr = symbolRegistry.findSymbol(table);
                CompoundBatch* batch = nullptr;
                if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<CompoundBatch>()) {
                  auto batchPtr = Batch::WritablePtr::asWritable(symbolPtr);
                  batch = static_cast<CompoundBatch*>(batchPtr.get());
                } else {
                  batch = new CompoundBatch(true);
                  symbolPtr = Batch::WritablePtr(batch);
                }
                batch->addColumn(column);
                return table;
              },
              tableBatchPtr, columnBatchPtr);
        });

    operatorRegistry.template argTypes<Symbol>().template registerFunction<1>(
        "RemoveTable", [&symbolRegistry](auto&& batchPtr) {
          return OperatorUtils::evaluateElements(
              [&symbolRegistry](auto const& table) -> Symbol {
                auto& symbolPtr = symbolRegistry.findSymbol(table);
                symbolPtr.reset();
                return table;
              },
              batchPtr);
        });
  }

  static void manageColumns() {
    auto& symbolRegistry = DefaultSymbolRegistry::instance();
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template argTypes<Symbol, Symbol>().template registerFunction<2>(
        "AddColumn", [&symbolRegistry](auto&& tableBatchPtr, auto&& columnBatchPtr) {
          return OperatorUtils::evaluateElements(
              [&symbolRegistry](auto const& table, auto const& column) -> Symbol {
                auto& symbolPtr = symbolRegistry.findSymbol(table);
                if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<CompoundBatch>()) {
                  auto batchPtr = Batch::WritablePtr::asWritable(symbolPtr);
                  auto& batch = static_cast<CompoundBatch&>(*batchPtr);
                  batch.addColumn(column);
                }
                return table;
              },
              tableBatchPtr, columnBatchPtr);
        });

    operatorRegistry.template argTypes<Symbol>().template registerFunction<1>(
        "Columns", [&symbolRegistry](auto&& symbolBatchPtr) -> Batch::ReadablePtr {
          auto& symbolPtr = symbolRegistry.findSymbol(*symbolBatchPtr->begin());
          if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<CompoundBatch>()) {
            auto const& batch = static_cast<CompoundBatch const&>(*symbolPtr);
            return Batch::ReadablePtr(batch.columns());
          }
          return Batch::WritablePtr(new ValueBatch<std::string>());
        });
  }

  static void manageRows() {
    auto& symbolRegistry = DefaultSymbolRegistry::instance();
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template argTypes<Symbol, ComplexExpression>().template registerFunction<2>(
        "InsertInto", [&symbolRegistry](auto symbolBatchPtr, auto rowBatchPtr) {
          auto& symbolPtr = symbolRegistry.findSymbol(*symbolBatchPtr->begin());
          if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<CompoundBatch>()) {
            BatchVisitDispatcher<CompoundBatch>::visit(
                [&rowBatchPtr](auto& batch) {
                  size_t numColumns = batch.numColumns();
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
                  batch.append(std::move(argBatches));
                },
                *Batch::WritablePtr::asWritable(symbolPtr));
          }
          return Batch::WritablePtr::asWritable(Batch::ReadablePtr(symbolBatchPtr));
        });
  }
};

} // namespace boss::engines::bulk
