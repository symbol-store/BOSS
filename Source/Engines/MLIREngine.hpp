#pragma once
#include "../Engine.hpp"

namespace boss::engines::mlir {
class Engine : public boss::Engine {

public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;
  Engine() = default;
  Expression::ReturnType evaluate(Expression const& e);
  ~Engine() = default;
};

}; // namespace boss::engines::mlir
