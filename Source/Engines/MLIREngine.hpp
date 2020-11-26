#pragma once
#include "../Engine.hpp"
#include "MLIREngine/Interface/Interface.hpp"

namespace boss::engines::mlir {
class Engine : public boss::Engine {

public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;
  Engine();
  Expression::ReturnType evaluate(Expression const& e);
  ~Engine();
};

}; // namespace boss::engines::mlir
