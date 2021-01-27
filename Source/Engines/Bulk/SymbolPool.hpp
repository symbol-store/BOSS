#pragma once

#include "Batch/Batch.hpp"

#include "../../Expression.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace boss::engines::bulk {

template <typename T> class SymbolPool {
private:
  SymbolPool() = default;

public:
  ~SymbolPool() = default;
  static SymbolPool& instance() {
    static SymbolPool instance;
    return instance;
  }

  using SymbolPtr = std::unique_ptr<T>;

  SymbolPtr& findSymbol(Symbol const& symbol) { return m_symbolMap[symbol.getName()]; }

  void registerSymbol(Symbol const& symbol, T& value) {
    m_symbolMap[symbol.getName()] = SymbolPtr(&value);
  }

private:
  using SymbolMapping = std::unordered_map<std::string, SymbolPtr>;

  SymbolMapping m_symbolMap;
};

using DefaultSymbolPool = SymbolPool<Batch const>;
using WritableBatchPool = SymbolPool<Batch>;

} // namespace boss::engines::bulk
