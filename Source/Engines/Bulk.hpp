#pragma once

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
  ~Engine();

  Expression evaluate(Expression const& e);

private:
  BatchFactory& batchFactory;
};

} // namespace boss::engines::bulk
