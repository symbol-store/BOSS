#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include <arrow/api.h>
#include <mlir/IR/Types.h>
#include "Storage.hpp"
#include "Strings.hpp"

namespace runtime::hash {

class HashTable {
public:
  explicit HashTable(size_t numChildStreams): maps(numChildStreams), relation(nullptr) {}

  ~HashTable() {
    delete relation;
  }

  void build();
  new_runtime::RelationBuilder& getBuilder() { return relationBuilder; }

  arrow::Array* getChildArray(int rightRelationIndx);
  std::unordered_map<size_t, std::vector<size_t>>* getChildIndexMap(int rightRelationIndex);
  std::map<std::string, arrow::DataType*> getChildFields(int rightRelationIndex);

  size_t getNumChildArrays() { return relation->get()->num_fields(); }

  arrow::Array* getRelation() { return relation->get().get(); }

private:

  // map from hash to index into right relation
  std::vector<std::unordered_map<size_t, std::vector<size_t>>> maps;
  // Store right tuples here
  new_runtime::Relation* relation;
  new_runtime::RelationBuilder relationBuilder;
  // Which fields are in the relation
//  std::vector<std::map<std::string, ::mlir::Type>> fields;
};

extern "C" size_t hash_Int(int);
extern "C" size_t hash_Bool(bool);
extern "C" size_t hash_String(boss::mlir::runtime::string::RuntimeString*);

extern "C" void hashTableInsert(runtime::hash::HashTable* table, size_t rightIndex, size_t hash,
                                  size_t value);
extern "C" size_t hashTableLookup(std::unordered_map<size_t, std::vector<size_t>>* map, size_t hash);

extern "C" HashTable* finalizeHashBuilder(HashTable* table);

}