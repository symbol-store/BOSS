#pragma once
#include "../Engine.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Engines/MLIREngine/Executors/Interpreter.hpp"

namespace boss::engines::mlir {
class Engine : public boss::Engine {
public:
  explicit Engine(new_runtime::Database& database) : database(&database), interpreter(&database) {
    ownsDatabase = false;
  }

  Engine(): database(new new_runtime::Database), interpreter(database) {
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
  new_runtime::Database* database;
  interpreter::Interpreter interpreter;
};

}; // namespace boss::engines::mlir
