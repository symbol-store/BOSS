#pragma once
#ifdef WSINTERFACE
#include "../Engine.hpp"
#include <wstp.h>
#include <string>

namespace boss::engines::wolfram {
class Engine : public boss::Engine {
private:
  class EngineImplementation& impl;
  friend class EngineImplementation;

public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = delete;
  Engine();
  Expression::ReturnType evaluate(Expression const& e);
  ~Engine();
};
} // namespace boss::engines::wolfram
#endif // WSINTERFACE
