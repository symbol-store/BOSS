#include "HashTable.hpp"

arrow::Array* runtime::hash::HashTable::getChildArray(int rightRelationIndx) {
  return relation->get()->field(rightRelationIndx).get();
}

std::unordered_map<size_t, std::vector<size_t>> const&
runtime::hash::HashTable::getChildIndexMap(int rightRelationIndex) {
  return maps[rightRelationIndex];
}

std::map<std::string, boss::mlir::types::RuntimeTypes> const&
runtime::hash::HashTable::getChildFields(int rightRelationIndex) {
  return fields[rightRelationIndex];
}

void runtime::hash::HashTable::build() {
  relation = relationBuilder.build();
}
