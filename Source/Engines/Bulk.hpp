#pragma once

#include "Bulk/BatchFactory.hpp"

#include "../Engine.hpp"
#include "../Expression.hpp"

namespace boss::engines::bulk {

class BatchFactory;
class Engine : public boss::Engine {
public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  Engine();
  ~Engine() = default;

  Expression evaluate(Expression const& e);

  static BatchFactory const& getBatchFactory() {
    static BatchFactory const* factoryInstance = nullptr;
    if(factoryInstance == nullptr) {
      factoryInstance = &createBatchFactory();
    }
    return *factoryInstance;
  }

private:
  static BatchFactory const& createBatchFactory();
};

} // namespace boss::engines::bulk
