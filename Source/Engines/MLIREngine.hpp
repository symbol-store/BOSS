#pragma once
#include "../Engine.hpp"
#include "Engines/MLIREngine/Runtime/Database.hpp"

namespace boss::engines::mlir {
class Engine : public boss::Engine {
  runtime::Database database;

public:
  explicit Engine(runtime::Database&& database) : database(std::move(database)) {}

  Engine() : database() {};

  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;
  Expression evaluate(Expression const& e);
  ~Engine() = default;
};

}; // namespace boss::engines::mlir
