#pragma once

#include "Batch/Batch.hpp"

#include "../../Expression.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace boss::engines::bulk {

template <typename BatchType> class SymbolRegistry {
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

  using SymbolPtr = ReadableBatchPtr<BatchType>;
  SymbolPtr& findSymbol(Symbol const& symbol) { return m_symbolMap[symbol.getName()]; }

  void registerSymbol(Symbol const& symbol, BatchType& value) {
    m_symbolMap[symbol.getName()] = SymbolPtr(&value);
  }

  void clear() { m_symbolMap.clear(); }

private:
  using SymbolMapping = std::unordered_map<std::string, SymbolPtr>;

  SymbolMapping m_symbolMap;
};

using DefaultSymbolRegistry = SymbolRegistry<Batch>;

} // namespace boss::engines::bulk
