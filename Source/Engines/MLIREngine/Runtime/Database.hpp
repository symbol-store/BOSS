#pragma once
#include "Expression.hpp"
#include "arrow/api.h"
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

class Table {
public:
  void bulk_load(std::shared_ptr<arrow::Schema> schema,
                 std::vector<std::map<std::string, boss::Expression>> tuples);

  std::shared_ptr<arrow::ChunkedArray> getColumnDataPtr(std::string name, bool symbolic);

private:
  std::shared_ptr<arrow::Table> data = nullptr;
};

class Database {
public:
private:
  std::map<std::string, Table> tables;
};
