#pragma once

#include "ExpressionBatch.hpp"

#include "../SymbolPool.hpp"

namespace boss::engines::bulk {

class FunctionBatch : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<FunctionBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  FunctionBatch(BatchFactory const& factory, std::vector<Symbol> const& parameters,
                CompoundBatch const& definitionBatch)
      : m_parameters(parameters), CompoundBatch(factory, false, Symbol("Function")) {
    insert(0, 0, definitionBatch.clone());
  }

  FunctionBatch(BatchFactory const& factory, std::vector<Symbol>&& parameters,
                CompoundBatch const& definitionBatch)
      : m_parameters(std::move(parameters)), CompoundBatch(factory, false, Symbol("Function")) {
    insert(0, 0, definitionBatch.clone());
  }

  FunctionBatch(FunctionBatch const& other, bool clear = false)
      : m_parameters(other.m_parameters), CompoundBatch(other, clear) {}

  BatchPtr clone(bool clear = false) const override {
    return BatchPtr(new FunctionBatch(*this, clear));
  }

  BatchPtr evaluateWith(std::vector<Batch const*> const& args) const {
    std::vector<std::pair<DefaultSymbolPool::SymbolPtr&, DefaultSymbolPool::SymbolPtr>> oldSymbols;
    oldSymbols.reserve(args.size());
    auto paramIt = m_parameters.begin();
    for(auto const& arg : args) {
      if(paramIt == m_parameters.end()) {
        break;
      }

      // store existing symbols
      // to retrieve later in case of name collision
      // (and make sure they are not destroyed while dereferenced...)
      auto& batchPtr = DefaultSymbolPool::instance().findSymbol(*paramIt);
      oldSymbols.emplace_back(batchPtr, std::move(batchPtr));

      // set symbol at the function scope
      // don't move those pointers directly! they are already owned somewhere else...
      batchPtr = DefaultSymbolPool::SymbolPtr(std::move(arg->clone()));

      ++paramIt;
    }

    BatchPtr evaluatedPtr = (*begin())->evaluate();

    // before finishing, set back any colliding symbol (or clear them)
    for(auto& oldSymbol : oldSymbols) {
      oldSymbol.first = std::move(oldSymbol.second);
    }

    return evaluatedPtr;
  }

  BatchPtr evaluate() const override {
    auto evaluatedPtr = CompoundBatch::evaluate();

    // put back missing info
    auto& functionBatch = *static_cast<FunctionBatch*>(evaluatedPtr.get());
    functionBatch.m_parameters.insert(functionBatch.m_parameters.begin(), m_parameters.begin(),
                                      m_parameters.end());

    return evaluatedPtr;
  }

private:
  std::vector<Symbol> m_parameters;
};

} // namespace boss::engines::bulk