#pragma once

#include "Batch/Batch.hpp"
#include "Batch/CompoundBatch.hpp"

#include <string>
#include <vector>

namespace boss::engines::bulk {

class TableView : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<TableView>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  explicit TableView(BatchFactory const& factory) : CompoundBatch(factory, true) {}
  TableView(TableView const& other, bool clear = false)
      : CompoundBatch(other, clear), m_columns(other.m_columns) {}

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(new TableView(*this, clear));
  }

  void addColumn(std::string const& name) { m_columns.push_back(name); }

  std::string const& columnName(size_t index) const { return m_columns[index]; }

  int columnIndex(std::string const& name) const {
    for(size_t index = 0; index < m_columns.size(); ++index) {
      if(m_columns[index] == name) {
        return static_cast<int>(index);
      }
    }
    return -1;
  }

  size_t numColumns() const { return m_columns.size(); }

  BatchPtr evaluate() const override {
    auto evaluatedPtr = CompoundBatch::evaluate();

    // put back missing info
    auto& tableView = *static_cast<TableView*>(evaluatedPtr.get());
    tableView.m_columns.insert(tableView.m_columns.end(), m_columns.begin(), m_columns.end());

    return evaluatedPtr;
  }

private:
  std::vector<std::string> m_columns;
};

} // namespace boss::engines::bulk
