#pragma once
#ifdef WSINTERFACE
#include "../Engine.hpp"
#include <wstp.h>

namespace boss::engines::wolfram {
class Engine : public boss::Engine {
private:
  WSENV environment = {};
  WSLINK link = {};

public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;
  Engine();
  Expression::ReturnType evaluate(Expression const& e);
  ~Engine();
  friend class EngineImplementation;
};
} // namespace boss::engines::wolfram
#endif // WSINTERFACE
