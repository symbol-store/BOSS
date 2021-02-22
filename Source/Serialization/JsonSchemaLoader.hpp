#pragma once

#include "../Expression.hpp"

#include <fstream>
#include <functional>
#include <string>

namespace boss::serialization {

class JsonSchemaLoader {
public:
  explicit JsonSchemaLoader(std::string const& filepath);

  template <typename Engine> bool loadTables(Engine& engine) {
    return loadTables([&engine](Expression const& expression) { engine.evaluate(expression); });
  };

private:
  std::ifstream m_iFileStream;

  bool loadTables(std::function<void(Expression const&)>&& evaluate);
};

} // namespace boss::serialization
