#pragma once
#include "Expression.hpp"
#include "arrow/api.h"
#include "Engines/MLIREngine/Types/Types.hpp"
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#include <map>

namespace runtime {

class Table {
public:
  void bulk_load(std::shared_ptr<arrow::Schema> schema,
                 std::vector<std::map<std::string, boss::Expression>> tuples);

  std::shared_ptr<arrow::ChunkedArray> getColumnDataPtr(std::string name, bool symbolic);

  bool operator==(const Table& other) const { return other.data == data; }

  std::shared_ptr<arrow::Schema> getSchema() {
    return data->schema();
  }

  size_t getLength() {
    return data->num_rows();
  }

  static Table fromArrowTable(std::shared_ptr<arrow::Table> arrowTable) {
    Table table;
    table.data = std::move(arrowTable);
    return table;
  }

private:
  std::shared_ptr<arrow::Table> data = nullptr;
};

class Database {
public:
  Database() = default;

  [[nodiscard]] Table const& getRelation(std::string const& name) const { return tables.find(name)->second; }

  void addRelation(std::string const& name, Table&& table) { tables[name] = std::move(table); }
private:
  std::map<std::string, Table> tables;
};

std::map<std::string, arrow::ArrayBuilder*>* getBuildersForSchema(arrow::Schema* schema);
extern "C" Table* constructTable(arrow::Schema*, std::map<std::string, arrow::ArrayBuilder*>* builders);
extern "C" void addToRelation_Int(arrow::ArrayBuilder* builder, int value);
extern "C" void addToRelation_Bool(arrow::ArrayBuilder* builder, bool value);
// TODO implement other addToRelation_X

} // namespace runtime
