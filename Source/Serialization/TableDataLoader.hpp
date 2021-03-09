#pragma once

#include "../Expression.hpp"
#include "../Utilities.hpp"

#include <functional>
#include <string>
#include <vector>

using boss::utilities::operator""_;

namespace boss::serialization {

class TableDataLoader {
public:
  template <typename Engine>
  static bool load(Engine& engine, Symbol const& table, std::string const& filepath) {
    if(filepath.rfind(".tbl") != std::string::npos) {
      return loadFromTBL(engine, table, filepath);
    }

    if(filepath.rfind(".csv") != std::string::npos) {
      return loadFromCSV(engine, table, filepath);
    }

    throw std::runtime_error("unsupported file format for " + filepath);
  }

  template <typename Engine>
  static bool loadFromTBL(Engine& engine, Symbol const& table, std::string const& filepath) {
    return load(TableInfo(engine, table), engine, filepath, '|', false);
  }

  template <typename Engine>
  static bool loadFromCSV(Engine& engine, Symbol const& table, std::string const& filepath) {
    return load(TableInfo(engine, table), engine, filepath, ',', true);
  }

private:
  template <typename Engine> struct TableInfo {
    TableInfo(Engine& engine, Symbol const& table) : table(table) {
      size_t const numColumns = std::get<int>(engine.evaluate("Length"_("Columns"_(table))));
      columnNames.reserve(numColumns);
      for(int index = 0; index < numColumns; ++index) {
        auto const& getColumnName = "Extract"_("Columns"_(table), index + 1);
        columnNames.emplace_back(std::get<std::string>(engine.evaluate(getColumnName)));
      }
    }

    Symbol const& table;
    std::vector<std::string> columnNames;
  };

  template <typename Engine>
  static bool load(TableInfo<Engine> const& info, Engine& engine, std::string const& filepath,
                   char separator, bool hasHeader) {
    return loadInternal(filepath, info.table, separator, hasHeader, info.columnNames,
                        [&engine](Expression const& expr) { engine.evaluate(expr); });
  }

  static bool loadInternal(std::string const& filepath, Symbol const& table, char separator,
                           bool hasHeader, std::vector<std::string> const& columnNames,
                           std::function<void(Expression const&)>&& evaluate);
};

} // namespace boss::serialization
