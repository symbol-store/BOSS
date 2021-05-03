
#include "JsonSchemaLoader.hpp"

#include "../Expression.hpp"
#include "../Utilities.hpp"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <fstream>
#include <functional>
#include <string>
#include <vector>

using boss::utilities::operator""_;

namespace boss::serialization {
  
JsonSchemaLoader::JsonSchemaLoader(std::string const& filepath) : m_iFileStream(filepath) {
  if(m_iFileStream.fail()) {
    throw std::runtime_error("failed to open " + filepath);
  }
}

bool JsonSchemaLoader::loadTables(std::function<void(Expression const&)>&& evaluate) {
  if(m_iFileStream.fail()) {
    throw std::runtime_error("file is not open");
  }

  json jsonSchema;
  m_iFileStream >> jsonSchema;

  for(auto const& jsonTable : jsonSchema) {
    auto const& tableName = jsonTable["name"].get<std::string>();
    auto const& columns = jsonTable["columns"];

    ExpressionArguments createTableArguments;
    createTableArguments.reserve(columns.size() + 1);
    createTableArguments.emplace_back(Symbol(tableName));

    for(auto const& jsonColumn : columns) {
      auto const& columnName = jsonColumn["name"].get<std::string>();
      createTableArguments.emplace_back(Symbol(columnName));
    }

    ComplexExpression createTable("CreateTable"_, createTableArguments);
    evaluate(createTable);
  }

  return true;
}

} // namespace boss::serialization
