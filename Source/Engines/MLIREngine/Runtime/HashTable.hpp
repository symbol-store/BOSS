#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include <arrow/api.h>
#include "Storage.hpp"

namespace runtime::hash {

struct HashTable {
  HashTable(): relation(nullptr) {}

  ~HashTable() {
    delete relation;
  }

  void build();

  // map from hash to index into right relation
  std::vector<std::unordered_map<size_t, std::vector<size_t>>> maps;
  // Store right tuples here
  new_runtime::Relation* relation;
  new_runtime::RelationBuilder relationBuilder;
  // Which fields are in the relation
  std::vector<std::map<std::string, boss::mlir::types::RuntimeTypes>> fields;

  arrow::Array* getChildArray(int rightRelationIndx);
  std::unordered_map<size_t, std::vector<size_t>> const& getChildIndexMap(int rightRelationIndex);
  std::map<std::string, boss::mlir::types::RuntimeTypes> const& getChildFields(int rightRelationIndex);
};

extern "C" size_t hash_Int(int);
extern "C" size_t hash_Bool(bool);

extern "C" size_t hash_lookup(HashTable* table, size_t rightIndex);
extern "C" size_t hash_insert(HashTable* table, size_t rightIndex, size_t hash, size_t value);

}