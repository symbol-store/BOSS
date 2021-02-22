#include "Database.hpp"

void Table::createNewBuffer() {
  int size = 1024;

  void* newBuffer = malloc(size);

  buffers.push_back({size, newBuffer});
}

SymbolArgumentType Table::getTypeForColumn(std::string& name) {
  return std::get<1>(*std::find_if(schemaBegin(), schemaEnd(),
                                   [&name](auto& t) { return std::get<0>(t) == name; }));
}
