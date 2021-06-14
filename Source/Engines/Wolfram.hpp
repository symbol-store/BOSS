#pragma once
#ifdef WSINTERFACE
#include "../Engine.hpp"
#include <string>
#include <wstp.h>

namespace boss::engines::wolfram {
using WolframExpressionSystem = ExtensibleExpressionSystem<std::vector<int>>;
using AtomicExpression = WolframExpressionSystem::AtomicExpression;
using ComplexExpression = WolframExpressionSystem::ComplexExpression;
using Expression = WolframExpressionSystem::Expression;
using ExpressionArguments = WolframExpressionSystem::ExpressionArguments;

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
  boss::Expression evaluate(Expression const& e);
  ~Engine();
};
} // namespace boss::engines::wolfram
#endif // WSINTERFACE
