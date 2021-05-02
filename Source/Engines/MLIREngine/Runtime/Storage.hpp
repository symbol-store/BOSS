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

class Relation;

class RelationBuilder {
  using Fields = std::map<std::string, boss::mlir::types::RuntimeTypes>;
public:
  RelationBuilder(): builder(std::make_shared<arrow::DenseUnionBuilder>(arrow::default_memory_pool())) {}

  std::shared_ptr<arrow::ArrayBuilder> getOrCreateColumnBuilder(std::string fieldName, Fields const& fields);

  std::shared_ptr<arrow::ArrayBuilder> getOrCreateTypedStructBuilder(Fields const& fields);

  Relation* build();

private:

  static std::shared_ptr<arrow::ArrayBuilder> builderForType(boss::mlir::types::RuntimeTypes type);

  // TODO create a dense union builder, then create a method that selects the correct child builder given the fields and field name

  // The builder for expression will need to be done dynamically at runtime because we don't know the full expression type

  std::shared_ptr<arrow::DenseUnionBuilder> builder;

  // TODO compare function
  // Stores child index in denseUnionBuilder for given fields
  std::map<Fields, uint8_t> fieldsToBuilder;
};


class Relation {
public:
  Relation(): relation(nullptr) {}

  explicit Relation(std::shared_ptr<arrow::DenseUnionArray> array) {
    relation = std::move(array);
  }

  void bulk_load(std::vector<std::map<std::string, boss::Expression>> const& tuples);

//  arrow::ChunkedArray* getColumn(std::string const& name) {
//    return data->GetColumnByName(name).get();
//  }

//  std::shared_ptr<arrow::Schema> getSchema() { return data->schema(); }

  [[nodiscard]] std::shared_ptr<arrow::DenseUnionArray> const& get() const { return relation; }

private:
  std::shared_ptr<arrow::DenseUnionArray> relation;
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
