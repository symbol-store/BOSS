#pragma once
#include "../Engine.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"

namespace boss::engines::mlir {
class Engine : public boss::Engine {
  new_runtime::Database database;

public:
  explicit Engine(new_runtime::Database&& database) : database(std::move(database)) {}

  Engine() : database() {};

  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;
  Expression evaluate(Expression const& e);
  ~Engine() = default;

  new_runtime::Database& getDatabase() { return database; }
};

}; // namespace boss::engines::mlir
