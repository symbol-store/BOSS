#pragma once

#include "../ArrowExtensions/CompoundArray.hpp"
#include "../Operator.hpp"
#include "../SymbolRegistry.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class DBManagementOps {
  using TableArgument = typename OperatorUtils::TableArgument;
  using ListArgument = typename OperatorUtils::ListArgument;

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
  class CreateTableOperator : public Operator<AllowedArguments<Symbol>> {
  public:
    BulkExpression evaluate(Symbol const& tableSymbol) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
      symbolRegistry.registerSymbol(tableSymbol, std::make_shared<CompoundArray>(true));
      return tableSymbol;
    }
  };

  class CreateTableAndColumnsOperator
      : public Operator<AllowedArguments<Symbol>, AllowedArguments<Symbol>> {
  public:
    template <typename TableSymbolType, typename ColumnSymbolType>
    BulkExpression evaluate(TableSymbolType const& tableSymbol,
                            ColumnSymbolType const& columnSymbol) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
      auto& symbolPtr = symbolRegistry.findSymbol(tableSymbol);
      auto* pointerToTableArray =
          symbolPtr ? std::get_if<std::shared_ptr<CompoundArray>>(symbolPtr.get()) : nullptr;
      if(pointerToTableArray == nullptr) {
        symbolRegistry.setSymbol(symbolPtr, std::make_shared<CompoundArray>(true));
        pointerToTableArray = &std::get<std::shared_ptr<CompoundArray>>(*symbolPtr);
      }
      auto& tableArray = **pointerToTableArray;
      tableArray.addColumn(columnSymbol);
      return tableSymbol;
    }
  };

  class RemoveTableOperator : public Operator<AllowedArguments<Symbol>> {
  public:
    template <typename TableSymbolType>
    BulkExpression evaluate(TableSymbolType const& tableSymbol) const {
      auto& symbolRegistry = DefaultSymbolRegistry::instance();
      symbolRegistry.clearSymbol(tableSymbol);
      return tableSymbol;
    }
  };

  class AddColumnOperator : public Operator<TableArgument, AllowedArguments<Symbol>> {
  public:
    template <typename TableType, typename ColumnSymbolType>
    BulkExpression evaluate(TableType const& tableArrayPtr,
                            ColumnSymbolType const& columnSymbol) const {
      tableArrayPtr->addColumn(columnSymbol);
      return tableArrayPtr;
    }
  };

  class ColumnsOperator : public Operator<TableArgument> {
  public:
    template <typename TableType> BulkExpression evaluate(TableType const& tableArrayPtr) const {
      return OperatorUtils::getColumnNames(*tableArrayPtr);
    }
  };

  class InsertIntoOperator : public OperatorEvaluateOnlyOnce<TableArgument, ListArgument> {
  public:
    template <typename TableArrayType, typename RowType>
    BulkExpression evaluate(TableArrayType const& tableArrayPtr,
                            RowType const& rowExpression) const {
      auto& tableArray = *tableArrayPtr;
      size_t numColumns = tableArray.numArguments();
      // make sure the row matches the column count
      // otherwise shrink or add missing columns
      if(rowExpression.getArguments().size() != numColumns) {
        BulkExpressionArguments argsToInsert;
        argsToInsert.reserve(numColumns);
        // copy existing columns
        auto rowIt = rowExpression.getArguments().begin();
        auto rowItEnd = rowExpression.getArguments().end();
        for(int i = 0; i < numColumns && rowIt != rowItEnd; ++i, ++rowIt) {
          argsToInsert.emplace_back(*rowIt);
        }
        // add missing columns
        Symbol missingSymbol("Missing");
        for(auto i = argsToInsert.size(); i < numColumns; ++i) {
          argsToInsert.emplace_back(missingSymbol);
        }
        tableArray.append(BulkComplexExpression(Symbol("List"), argsToInsert));
      } else {
        tableArray.append(rowExpression);
      }
      return tableArrayPtr;
    }
  };
};

} // namespace boss::engines::bulk
