#pragma once
#include "Engines/MLIREngine/Types/Types.hpp"
#include "Expression.hpp"
#include "arrow/api.h"
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace new_runtime {

class Relation {
public:
  void bulk_load(std::vector<std::map<std::string, boss::Expression>> const& tuples);

//  arrow::ChunkedArray* getColumn(std::string const& name) {
//    return data->GetColumnByName(name).get();
//  }

//  std::shared_ptr<arrow::Schema> getSchema() { return data->schema(); }

  std::shared_ptr<arrow::Array> get() { return relation; }

private:
  std::shared_ptr<arrow::Array> relation;
};

class Database {
public:
  Database() = default;

  [[nodiscard]] Relation const& getRelation(std::string const& name) const {
    return relations.find(name)->second;
  }

  void addRelation(std::string const& name, Relation&& table) {
    relations[name] = std::move(table);
  }

private:
  std::map<std::string, Relation> relations;
};

} // namespace new_runtime
