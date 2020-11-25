#pragma once

#include "Bulk/Table.hpp"

#include "Bulk/SymbolicData/InterpolatedData.hpp"
#include "Bulk/SymbolicData/FetchedData.hpp"

#include <unordered_map>

namespace boss::engines::bulk {

class Engine {
private:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;

public:
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;
  Engine();
  ~Engine();

  typedef char const* StringType;
  typedef TableType<int, float, StringType> SymbolicTableType;
  typedef typename SymbolicTableType::template Table<> SymbolicTable;

  typedef InterpolatedData<MaterializedView<SymbolicTable>, int, int> InterpolatedInt;

  typedef FetchedData<MaterializedView<SymbolicTable>, int> FetchedInt;
  typedef FetchedData<MaterializedView<SymbolicTable>, float> FetchedFloat;

  SymbolicTable& table(std::string const& tableName) {
    return map.try_emplace(tableName, tableName).first->second;
  }

  void extractTableNames(std::vector<std::string>& table_names) {
    table_names.clear();
    for(auto& [key, value] : map) {
      table_names.push_back(key);
    }
  }

private:
  std::unordered_map<std::string, SymbolicTable> map;
};

} // namespace boss::engines::bulk
