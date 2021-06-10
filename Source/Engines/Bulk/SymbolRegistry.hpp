#pragma once

#include "BulkExpression.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace boss::engines::bulk {

/** Keep any type of expression stored (including columns/tables as value/compound arrays)
 * and mapped to a symbol. */
template <typename T> class SymbolRegistry {
private:
  SymbolRegistry() = default;

public:
  ~SymbolRegistry() = default;
  SymbolRegistry(SymbolRegistry const& other) = delete;
  SymbolRegistry(SymbolRegistry&& other) = delete;
  SymbolRegistry& operator=(SymbolRegistry const& other) = delete;
  SymbolRegistry& operator=(SymbolRegistry&& other) = delete;

  static SymbolRegistry& instance() {
    static SymbolRegistry instance;
    return instance;
  }

  using StoredType = T;
  using StoredTypePtr = std::unique_ptr<StoredType>;

  void registerSymbol(Symbol const& symbol, StoredType const& value) {
    symbolMap[symbol.getName()] = std::make_unique<StoredType>(value);
  }

  void clearSymbol(Symbol const& symbol) { symbolMap[symbol.getName()].reset(); }

  StoredTypePtr& findSymbol(Symbol const& symbol) { return symbolMap[symbol.getName()]; }

  void setSymbol(StoredTypePtr& position, StoredType const& value) {
    position = std::make_unique<StoredType>(value);
  }

  void clear() { symbolMap.clear(); }

private:
  using SymbolMapping = std::unordered_map<std::string, StoredTypePtr>;

  SymbolMapping symbolMap;
};

using DefaultSymbolRegistry = SymbolRegistry<BulkExpression>;

} // namespace boss::engines::bulk
