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

  // expressions

  Expression evaluate(Expression const& e);

private:
  BatchFactory& m_batchFactory;
  static BatchFactory& createBatchFactory();
};

} // namespace boss::engines::bulk
