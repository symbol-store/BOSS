#pragma once

#include "../Engine.hpp"
#include "../Expression.hpp"
#include "../Utilities.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using boss::utilities::operator""_;

namespace boss::serialization {

class TableDataLoader {
public:
  static bool load(Engine& engine, Symbol const& table, std::string const& filepath) {
    if(filepath.rfind(".tbl") != std::string::npos) {
      return loadFromTBL(engine, table, filepath);
    }

    if(filepath.rfind(".csv") != std::string::npos) {
      return loadFromCSV(engine, table, filepath);
    }

    throw std::runtime_error("unsuppported file format for " + filepath);
  }

  static bool loadFromTBL(Engine& engine, Symbol const& table, std::string const& filepath) {
    return load(TableInfo(engine, table), engine, filepath, '|', false);
  }

  static bool loadFromCSV(Engine& engine, Symbol const& table, std::string const& filepath) {
    return load(TableInfo(engine, table), engine, filepath, ',', true);
  }

private:
  struct TableInfo {
    TableInfo(Engine& engine, Symbol const& table) : table(table) {
      size_t numColumns = std::get<int>(engine.evaluate("ColumnCount"_(table)));
      for(int index = 0; index < numColumns; ++index) {
        auto const& getColumnName = "Extract"_("Columns"_(table), index + 1);
        auto columnName = std::get<std::string>(engine.evaluate(getColumnName));
        columnIndices[columnName] = index;
      }
    }

    Symbol const& table;
    std::map<std::string, int> columnIndices;
  };

  static void readColumnHeader(std::ifstream& iFileStream, TableInfo const& info,
                               std::vector<int>& outputIndices, char separator) {
    if(!iFileStream.good()) {
      return;
    }

    std::string header;
    if(!std::getline(iFileStream, header)) {
      return;
    }

    std::istringstream headerStream(header);

    std::string columnName;
    while(std::getline(headerStream, columnName, separator)) {
      int columnIndex = -1; // -1 means ignored

      // check for whitespaces (and trim)
      if(!columnName.empty()) {
        size_t startPos = columnName.find_first_not_of(" \r\n\t");
        size_t endPos = columnName.find_last_not_of(" \r\n\t");
        columnName = columnName.substr(startPos, 1 + endPos - startPos);
      }

      if(columnName.empty()) {
        // allow empty headers as far as we can keep count of column indices
        if(outputIndices.empty() || outputIndices.back() == (int)outputIndices.size() - 1) {
          columnIndex = static_cast<int>(outputIndices.size());
        } else {
          std::cerr << "WARNING: empty column name at column #" << columnIndex << std::endl;
        }
      } else {
        // try to find the column name in the schema, if not ignore
        auto it = info.columnIndices.find(columnName);
        if(it != info.columnIndices.end()) {
          columnIndex = it->second;
        } else {
          std::cerr << "WARNING: unrecognised column name '";
          std::cerr << columnName << "'" << std::endl;
        }
      }

      outputIndices.push_back(columnIndex);
    }
  }

  static bool readRow(std::ifstream& iFileStream, std::vector<std::string>& outputValues,
                      std::vector<int> const& indices, char separator) {
    std::string line;
    if(!std::getline(iFileStream, line)) {
      // eof
      return false;
    }

    if(line.back() == '\r') {
      line.resize(line.size() - 1);
    }

    if(line.empty()) {
      // skip any empty line
      return false;
    }

    std::istringstream lineStream(line);

    outputValues.resize(indices.size());
    for(int index : indices) {
      std::string nextValue;
      if(std::getline(lineStream, nextValue, separator)) {
        if(index >= 0) {
          outputValues[index] = nextValue;
        }
      }
    }

    return true;
  }

  static bool load(TableInfo const& info, Engine& engine, std::string const& filepath,
                   char separator, bool hasHeader) {
    std::ifstream iFileStream(filepath, std::ios::in);
    if(iFileStream.fail()) {
      throw std::runtime_error("failed to open " + filepath);
    }

    // TODO: use numRows to reserve
    size_t numRows = hasHeader ? 0 : 1; // count eof too (but not column header)
    std::string unused;
    while(std::getline(iFileStream, unused)) {
      ++numRows;
    }
    // rewind
    iFileStream.clear();
    iFileStream.seekg(0);

    size_t numColumns = info.columnIndices.size();

    std::vector<int> indices;
    indices.reserve(numColumns);

    if(hasHeader) {
      readColumnHeader(iFileStream, info, indices, separator);
    } else {
      for(int i = 0; i < numColumns; ++i) {
        indices.push_back(i);
      }
    }

    std::vector<std::string> rowValues;
    rowValues.reserve(numColumns);

    while(iFileStream.good()) {
      rowValues.clear();
      if(!readRow(iFileStream, rowValues, indices, separator)) {
        continue;
      }

      // then create a tuple from those values

      ExpressionArguments expressionRow;
      expressionRow.reserve(info.columnIndices.size());

      for(auto const& rowValueStr : rowValues) {
        // convert string to the right column type
        if(!rowValueStr.empty()) {
          // let this internal function decide which type is it
          expressionRow.emplace_back("Convert"_(rowValueStr));
          continue;
        }

        // default: add as missing value
        // TODO: get from schema what to do for missing data
        expressionRow.emplace_back("Missing"_);
      }

      auto insertRow = "InsertInto"_(info.table, (("List"_(std::move(expressionRow)))));
      engine.evaluate(insertRow);
    }

    return true;
  }
};

} // namespace boss::serialization
