#pragma once

#include "Batch/Batch.hpp"

#include "../../Expression.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace boss::engines::bulk {

template <typename BatchType> class SymbolPool {
private:
  SymbolPool() = default;

public:
  ~SymbolPool() = default;
  SymbolPool(SymbolPool const& other) = delete;
  SymbolPool(SymbolPool&& other) = delete;
  SymbolPool& operator=(SymbolPool const& other) = delete;
  SymbolPool& operator=(SymbolPool&& other) = delete;

  static SymbolPool& instance() {
    static SymbolPool instance;
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

using DefaultSymbolPool = SymbolPool<Batch>;

} // namespace boss::engines::bulk
