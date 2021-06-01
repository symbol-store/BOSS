#pragma once

#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class DBManagementOps {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<CreateTableOperator>("CreateTable");
    operatorRegistry.template registerOperator<CreateTableAndColumnsOperator>("CreateTable");
    operatorRegistry.template registerOperator<RemoveTableOperator>("RemoveTable");
    operatorRegistry.template registerOperator<AddColumnOperator>("AddColumn");
    operatorRegistry.template registerOperator<ColumnsOperator>("Columns");
    operatorRegistry.template registerOperator<InsertIntoOperator>("InsertInto");
  }

private:
  class CreateTableOperator : public OperatorBuilder<1>::OperatorForTypes<Symbol> {
  public:
    template <typename TableBatchType> auto evaluate(TableBatchType&& tableBatchPtr) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
      return OperatorUtils::evaluateElements(
          [&symbolRegistry](auto const& table) -> Symbol {
            auto& symbolPtr = symbolRegistry.findSymbol(table);
            symbolPtr = Batch::WritablePtr(new CompoundBatch(true));
            return table;
          },
          tableBatchPtr);
    }
  };

  class CreateTableAndColumnsOperator : public OperatorBuilder<2>::OperatorForTypes<Symbol> {
  public:
    template <typename TableBatchType, typename ColumnType>
    auto evaluate(TableBatchType&& tableBatchPtr, ColumnType&& columnBatchPtr) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
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
    }
  };

  class RemoveTableOperator : public OperatorBuilder<1>::OperatorForTypes<Symbol> {
  public:
    template <typename BatchType> auto evaluate(BatchType&& batchPtr) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
      return OperatorUtils::evaluateElements(
          [&symbolRegistry](auto const& table) -> Symbol {
            auto& symbolPtr = symbolRegistry.findSymbol(table);
            symbolPtr.reset();
            return table;
          },
          batchPtr);
    }
  };

  class AddColumnOperator : public OperatorBuilder<2>::OperatorForTypes<Symbol> {
  public:
    template <typename TableBatchType, typename ColumnType>
    auto evaluate(TableBatchType&& tableBatchPtr, ColumnType&& columnBatchPtr) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
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
    }
  };

  class ColumnsOperator : public OperatorBuilder<1>::OperatorForTypes<Symbol> {
  public:
    template <typename BatchType> Batch::ReadablePtr evaluate(BatchType&& symbolBatchPtr) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
      auto& symbolPtr = symbolRegistry.findSymbol(*symbolBatchPtr->begin());
      if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<CompoundBatch>()) {
        auto const& batch = static_cast<CompoundBatch const&>(*symbolPtr);
        return Batch::ReadablePtr(batch.columns());
      }
      return Batch::WritablePtr(new ValueBatch<std::string>());
    }
  };

  class InsertIntoOperator
      : public OperatorBuilder<2>::OperatorForTypesInOrder<Symbol, ComplexExpression> {
  public:
    template <typename SymbolBatchType, typename RowBatchType>
    auto evaluate(SymbolBatchType&& symbolBatchPtr, RowBatchType&& rowBatchPtr) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
      auto& symbolPtr = symbolRegistry.findSymbol(*symbolBatchPtr->begin());
      if(symbolPtr && symbolPtr->typeId() == UniqueId::forType<CompoundBatch>()) {
        BatchVisitDispatcher<CompoundBatch>::visit(
            [&rowBatchPtr](auto& batch) {
              size_t numColumns = batch.numColumns();
              size_t numArgsToInsert = rowBatchPtr->numArguments();
              size_t numRowsToInsert = numArgsToInsert > 0 ? (*rowBatchPtr->begin())->size() : 0;
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
    }
  };
};

} // namespace boss::engines::bulk
