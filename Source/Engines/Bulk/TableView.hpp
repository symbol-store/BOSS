#pragma once

#include "Dispatcher.hpp"

#include "Batch/Batch.hpp"
#include "Batch/CompoundBatch.hpp"

#include "../../Utilities.hpp"

#include <memory>
#include <string>
#include <vector>

namespace boss::engines::bulk {

using boss::utilities::operator""_;

template <typename... SupportedTypes> class TableView : public Batch {
public:
  using CompoundBatch = CompoundBatch<SupportedTypes...>;

  using ValueType = ComplexExpression;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<TableView>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool isRLE() const override { return false; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  TableView(BatchFactory const& factory) : m_dispatcher(factory) {}
  TableView(TableView const& other, bool clear = false)
      : m_dispatcher(other.m_dispatcher, clear), m_columns(other.m_columns) {}

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(new TableView(*this, clear));
  }
  void clear() override { m_dispatcher.clear(true); }

  virtual size_t size() const override { return m_dispatcher.size(); }

  auto begin() const {
    // very ugly... just temporary
    static BatchPtr batchPtr;
    batchPtr = evaluate();

    return static_cast<CompoundBatch*>(batchPtr.get())->begin();
  }
  auto end() const { return typename CompoundBatch::ConstIteratorImpl("List"_, {}, size()); }

  void addColumn(std::string name) { m_columns.push_back(name); }

  std::string const& columnName(size_t index) const { return m_columns[index]; }
  int columnIndex(std::string name) const {
    for(size_t index = 0; index < m_columns.size(); ++index) {
      if(m_columns[index] == name) {
        return index;
      }
    }
    return -1;
  }

  size_t numColumns() const { return m_columns.size(); }

  void insert(Expression const& row) override { m_dispatcher.insert(row); }
  void insert(Expression const& key, BatchPtr batch) { m_dispatcher.insert(key, std::move(batch)); }

  BatchPtr asListofColumns() const {
    BatchPtr outputPtr;
    for(auto& evaluatedPtr : m_dispatcher.evaluate()) {
      if(evaluatedPtr.get()->typeId() == UniqueId::forType<CompoundBatch>()) {
        auto& evaluated = *static_cast<CompoundBatch*>(evaluatedPtr.get());
        if(!outputPtr) {
          outputPtr = evaluated.clone();
        } else {
          for(auto evalIt = evaluated.begin(); evalIt != evaluated.end(); ++evalIt) {
            outputPtr.get()->insert(*evalIt);
          }
        }
      }
    }

    if(!outputPtr) {
      outputPtr = BatchPtr(new CompoundBatch("List"_, {}));
    }
    return outputPtr;
  }

  BatchPtr evaluate() const override { return asListofColumns(); }

  template <typename Func> void visitBatches(Func&& visitor) const {
    m_dispatcher.visitBatches(visitor);
  }

private:
  Dispatcher m_dispatcher;
  std::vector<std::string> m_columns;
};

} // namespace boss::engines::bulk
