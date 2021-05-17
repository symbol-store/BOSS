#pragma once
#include "../Engine.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"

namespace boss::engines::mlir {
class Engine : public boss::Engine {
  new_runtime::Database* database;

public:
  explicit Engine(new_runtime::Database& database) : database(&database) {
    ownsDatabase = false;
  }

  Engine() {
      database = new new_runtime::Database;
      ownsDatabase = true;
  };

  ~Engine() override {
    if(ownsDatabase) {
      delete database;
    }
  }

  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;
  Expression evaluate(Expression const& e);

  new_runtime::Database& getDatabase() { return *database; }

private:
  bool ownsDatabase;
};

}; // namespace boss::engines::mlir
