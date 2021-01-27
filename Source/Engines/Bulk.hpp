#pragma once

#include "Bulk/BatchTemplates.hpp"
#include "Bulk/BuiltinFunctions.hpp"

#include "../Engine.hpp"
#include "../Expression.hpp"

namespace boss::engines::bulk {

class Engine : public boss::Engine {
public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  Engine() { BuiltinFunctions functionsInitialiser(m_batchTemplates); }
  ~Engine() = default;

  // expressions

  Expression evaluate(Expression const& e);

private:
  using BatchTemplatesImpl = BatchTemplates<bool, int, float, std::string>;

  BatchTemplatesImpl m_batchTemplates;
};

} // namespace boss::engines::bulk
