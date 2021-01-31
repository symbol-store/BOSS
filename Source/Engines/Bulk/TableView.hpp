#pragma once

#include "Batch/Batch.hpp"
#include "Batch/CompoundBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include <string>
#include <vector>

namespace boss::engines::bulk {

class TableView : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<TableView>();

  using ColumnBatchType = ValueBatch<std::string>;
  using ColumnBatchPtr = std::unique_ptr<ColumnBatchType>;

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  explicit TableView(BatchFactory const& factory)
      : CompoundBatch(factory, true, false), m_columns(new ColumnBatchType()) {}

  TableView(TableView const& other, bool clear = false)
      : CompoundBatch(other, clear), m_columns(other.m_columns->cloneAsValueBatch()) {}

  ~TableView() override = default;
  TableView(TableView&& other) = delete;
  TableView& operator=(TableView const& other) = delete;
  TableView& operator=(TableView&& other) = delete;

  BatchPtr clone(bool clear = false) const override { return cloneAsTableView(clear); }

  using CompoundBatch::CompoundBatchPtr;
  CompoundBatchPtr cloneAsCompoundBatch(bool clear = false) const override {
    return cloneAsTableView(clear);
  }

  using TableViewPtr = std::unique_ptr<TableView>;
  TableViewPtr cloneAsTableView(bool clear = false) const {
    return TableViewPtr(new TableView(*this, clear));
  }

  void addColumn(std::string const& name) { m_columns->insert(name); }

  ColumnBatchPtr const& columns() { return m_columns; }

  std::string const& columnName(size_t index) const {
    return *std::next(m_columns->begin(), index); //NOLINT
  }

  int columnIndex(std::string const& name) const {
    int index = 0;
    for(auto const& columnName : *m_columns) {
      if(columnName == name) {
        return index;
      }
      ++index;
    }
    return -1;
  }

  size_t numColumns() const { return m_columns->size(); }

  BatchPtr evaluate() const override {
    auto evaluatedPtr = CompoundBatch::evaluate();

    // put back missing info
    BatchHelper<TableView>::visit(
        [this](auto& tableView) { tableView.m_columns = m_columns->cloneAsValueBatch(); },
        *evaluatedPtr);

    return evaluatedPtr;
  }

private:
  ColumnBatchPtr m_columns;
};

} // namespace boss::engines::bulk
