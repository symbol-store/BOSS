#pragma once

#include "../Engine.hpp"

#include <fstream>
#include <string>

namespace boss::serialization {

class JsonSchemaLoader {
public:
  explicit JsonSchemaLoader(std::string const& filepath);
  bool loadTables(Engine& engine);

private:
  std::ifstream m_iFileStream;
};

} // namespace boss::serialization
