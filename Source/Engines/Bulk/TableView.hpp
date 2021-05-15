#pragma once

#include "Batch/Batch.hpp"
#include "Batch/CompoundBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include <string>
#include <vector>

namespace boss::engines::bulk {

// [ISSUE] now that TableView contains no particular metadata,
// see if we can get rid of it and support it with normal CompoundBatch
/** TableView is an extension to the CompoundBatch
 * to support specific functions for accessing column information.
 * Also, the main difference with a CompoundBatch is that it passes the decomposed flag
 * to handle the logifc to store a row differently than a list of lists.
 * One additional purpose of having this class is to allow an operator
* to specifically take a TableView as argument (for the query oeprators). */
class TableView : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<TableView>();

  using ColumnBatchType = ValueBatch<Symbol>;
  using ColumnWritablePtr = WritableBatchPtr<ColumnBatchType>;
  using ColumnReadablePtr = ReadableBatchPtr<ColumnBatchType>;

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  explicit TableView(BatchFactory const& factory)
      : CompoundBatch(factory, true), m_columns(new ColumnBatchType()) {}

  TableView(TableView const& other, bool clear = false)
      : CompoundBatch(other, clear), m_columns(other.m_columns->cloneAsValueBatch()) {}

  ~TableView() override = default;
  TableView(TableView&& other) = delete;
  TableView& operator=(TableView const& other) = delete;
  TableView& operator=(TableView&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsTableView(clear));
  }
  WritableBatchPtr<CompoundBatch> cloneAsCompoundBatch(bool clear = false) const override {
    return WritableBatchPtr<CompoundBatch>(cloneAsTableView(clear));
  }
  virtual WritableBatchPtr<TableView> cloneAsTableView(bool clear = false) const {
    return WritableBatchPtr(new TableView(*this, clear));
  }

  template <typename BatchType, std::enable_if_t<std::is_base_of_v<BatchType, TableView>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsTableView(clear);
  }

  void addColumn(Symbol const& column) { m_columns->insert(column); }

  ColumnWritablePtr& columns() { return m_columns; }
  ColumnReadablePtr columns() const { return m_columns; }

  auto const& columnName(size_t index) const {
    return Symbol(*(columns()->begin() + index)).getName(); // NOLINT
  }

  int columnIndex(std::string const& name) const {
    int index = 0;
    for(Symbol const& column : *columns()) {
      if(column.getName() == name) {
        return index;
      }
      ++index;
    }
    return -1;
  }

  size_t numColumns() const { return columns()->size(); }

  bool evaluate(ReadablePtr& outputPtr) const override {
    // set the local columns to be accessible by the rows evaluation
    auto& symbolPtr = DefaultSymbolPool::instance().findSymbol(Symbol("$columns"));
    auto backupSymbol = std::move(symbolPtr);
    symbolPtr = columns();

    ReadablePtr evaluatedPtr;
    bool evaluated = CompoundBatch::evaluate(evaluatedPtr);

    // reset to any previous local columns symbol
    symbolPtr = std::move(backupSymbol);

    if(!evaluated) {
      outputPtr.reset();
      return false;
    }

    // put back missing info
    auto writablePtr = WritablePtr::asWritable(evaluatedPtr);
    BatchHelper<TableView>::visit(
        [this](auto& tableView) { tableView.m_columns = m_columns->cloneAsValueBatch(); },
        *writablePtr);

    outputPtr = std::move(writablePtr);
    return true;
  }

private:
  ColumnWritablePtr m_columns;
};

} // namespace boss::engines::bulk
