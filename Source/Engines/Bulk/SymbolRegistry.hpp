#pragma once

#include "Batch/Batch.hpp"
#include "ExtendedExpression.hpp"

#include "../../Expression.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace boss::engines::bulk {

/** Keep any type of batch stored and map to a symbol. */
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
  using SymbolPtr = std::unique_ptr<StoredType>;

  SymbolPtr& findSymbol(Symbol const& symbol) { return m_symbolMap[symbol.getName()]; }

  void registerSymbol(Symbol const& symbol, StoredType const& value) {
    m_symbolMap[symbol.getName()] = std::make_unique<StoredType>(value);
  }
  
  void registerSymbol(Symbol const& symbol, SymbolPtr&& symbolPtr) {
    m_symbolMap[symbol.getName()] = std::move(symbolPtr);
  }

  void clear() { m_symbolMap.clear(); }

private:
  using SymbolMapping = std::unordered_map<std::string, SymbolPtr>;

  SymbolMapping m_symbolMap;
};

using DefaultSymbolRegistry = SymbolRegistry<BulkExpression>;

} // namespace boss::engines::bulk
