#pragma once

#include "SymbolRegistry.hpp"

#include "../Bulk.hpp"
#include "Batch/Batch.hpp"
#include "Batch/CompoundBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include <string>
#include <vector>

namespace boss::engines::bulk {

// [https://github.com/symbol-store/BOSS/issues/89]
/** TableView is an extension to the CompoundBatch
 * to support specific functions for accessing column information.
 * Also, the main difference with a CompoundBatch is that it passes the decomposed flag
 * to handle the logic to store a row differently than a list of lists.
 * One additional purpose of having this class is to allow an operator
 * to specifically take a TableView as argument (for the query oeprators). */
class TableView : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<TableView>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  explicit TableView() : CompoundBatch(true) {}
  TableView(TableView const& other, bool clear = false) : CompoundBatch(other, clear) {}

  ~TableView() override = default;
  TableView(TableView&& other) = delete;
  TableView& operator=(TableView const& other) = delete;
  TableView& operator=(TableView&& other) = delete;

  Batch* clone(bool clear = false) const override { return cloneAsTableView(clear); }

  CompoundBatch* cloneAsCompoundBatch(bool clear = false) const override {
    return cloneAsTableView(clear);
  }

  virtual TableView* cloneAsTableView(bool clear = false) const {
    return new TableView(*this, clear);
  }

  template <typename BatchType, std::enable_if_t<std::is_base_of_v<BatchType, TableView>, int> = 0>
  BatchType* cloneAs(bool clear = false) const {
    return cloneAsTableView(clear);
  }

  void addColumn(Symbol const& column) { CompoundBatch::addArgument(column.getName()); }

  Batch::WritablePtr columns() const {
    // create a temporary column batch (compound) from the array fields
    auto const& batchData = CompoundBatch::data();
    ExpressionArguments columns;
    if(batchData.builder || !batchData.arrays.chunks().empty()) {
      auto type = batchData.builder ? batchData.builder->type() : batchData.arrays.chunk(0)->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      columns.reserve(structType->num_fields());
      for(auto const& field : structType->fields()) {
        columns.emplace_back(Symbol(field->name()));
      }
    }
    ComplexExpression columnList("List"_, columns);
    return Batch::WritablePtr(Engine::getBatchFactory().createBatch(columnList));
  }

  size_t numColumns() const {
    auto const& data = CompoundBatch::data();
    if(data.builder) {
      return data.builder->num_children();
    }
    if(!data.arrays.chunks().empty()) {
      return data.arrays.chunk(0)->num_fields();
    }
    return 0;
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    // set the local columns to be accessible by the rows evaluation
    auto& symbolPtr = DefaultSymbolRegistry::instance().findSymbol(Symbol("$columns"));
    auto backupSymbol = std::move(symbolPtr);
    symbolPtr = columns();

    bool evaluated = CompoundBatch::evaluate(outputPtr);

    // reset to any previous local columns symbol
    symbolPtr = std::move(backupSymbol);

    if(!evaluated) {
      outputPtr.reset();
      return false;
    }
    return true;
  }
};

} // namespace boss::engines::bulk
