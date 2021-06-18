#include "HashTable.hpp"

arrow::Array* runtime::hash::HashTable::getChildArray(int rightRelationIndex) {
  return relation->get()->field(rightRelationIndex).get();
}

std::unordered_map<size_t, std::vector<size_t>>*
runtime::hash::HashTable::getChildIndexMap(int rightRelationIndex) {
  return &maps[rightRelationIndex];
}

std::map<std::string, arrow::DataType*>
runtime::hash::HashTable::getChildFields(int rightRelationIndex) {
  auto structArray = std::dynamic_pointer_cast<arrow::StructArray>(relation->get()->field(rightRelationIndex));

  std::map<std::string, arrow::DataType*> fields;

  for (auto const& field : structArray->struct_type()->fields()) {
    fields[field->name()] = field->type().get();
  }

  return fields;
}

void runtime::hash::HashTable::build() {
  relation = relationBuilder.build();
}

size_t runtime::hash::hash_Int(int value) {
  auto hash = llvm::hash_value(value);
  return hash;
}

size_t runtime::hash::hash_Bool(bool value) {
  return llvm::hash_value(value);
}

size_t runtime::hash::hash_String(boss::mlir::runtime::string::RuntimeString* s) {
  return llvm::hash_value(std::string(s->data, 0, s->length));
}

void runtime::hash::hashTableInsert(runtime::hash::HashTable* table, size_t rightIndex, size_t hash,
                                  size_t value) {
  auto childTable = table->getChildIndexMap(rightIndex);

  childTable->operator[](hash) = std::vector<size_t>{value};
}

runtime::hash::HashTable* runtime::hash::finalizeHashBuilder(runtime::hash::HashTable* table) {
  table->build();
  return table;
}

size_t runtime::hash::hashTableLookup(std::unordered_map<size_t, std::vector<size_t>>* map, size_t hash) {
  auto it = map->find(hash);
  if (it == map->end()) {
    return -1;
  }
  return it->second[0];
}
