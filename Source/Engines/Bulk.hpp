#pragma once

#include "Bulk/BatchTemplates.hpp"
#include "Bulk/BuiltinFunctions.hpp"
#include "Bulk/Dispatcher.hpp"

#include "../Engine.hpp"
#include "../Expression.hpp"

namespace boss::engines::bulk {

class Engine : public boss::Engine {
public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  Engine() : m_batchTemplates(), m_dispatcher(m_batchTemplates) {
    BuiltinFunctions functionsInitialiser(m_batchTemplates);
  }
  ~Engine() {}

  // expressions

  Expression::ReturnType evaluate(Expression const& e);

private:
  BatchTemplates m_batchTemplates;
  Dispatcher m_dispatcher;
};

} // namespace boss::engines::bulk
