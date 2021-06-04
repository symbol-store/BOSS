#pragma once

#include "bulk/ExtendedExpression.hpp"

#include "../Engine.hpp"
#include "../Expression.hpp"

namespace boss::engines::bulk {

class Engine : public boss::Engine {
public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  Engine();
  ~Engine() = default;

  static Expression evaluate(Expression const& e);

  static BulkExpression createArray(arrow::ArrayVector&& arrays,
                                    std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder,
                                    CompoundArray const* parent = nullptr, size_t childIndex = 0);
};

} // namespace boss::engines::bulk
