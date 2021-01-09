#pragma once

#include "CompoundBatch.hpp"

#include "../SymbolPool.hpp"

namespace boss::engines::bulk {

template <typename... SupportedTypes>
class FunctionBatch : public CompoundBatch<SupportedTypes...> {
public:
  using CompoundBatch = CompoundBatch<SupportedTypes...>;

  using ValueType = ComplexExpression;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<FunctionBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  FunctionBatch(std::vector<Symbol> const& parameters, CompoundBatch const& definitionBatch)
      : m_parameters(parameters),
        CompoundBatch(Symbol("Function"), createCompountBatchList(definitionBatch)) {}

  FunctionBatch(std::vector<Symbol>&& parameters, CompoundBatch const& definitionBatch)
      : m_parameters(std::move(parameters)),
        CompoundBatch(Symbol("Function"), createCompountBatchList(definitionBatch)) {}

  FunctionBatch(FunctionBatch const& other, bool clear = false)
      : m_parameters(other.m_parameters), CompoundBatch(other, clear) {}

  BatchPtr clone(bool clear = false) const override { return BatchPtr(new FunctionBatch(*this, clear)); }

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

    BatchPtr evaluatedPtr = this->m_batches.front().get()->evaluate();

    // before finishing, set back any colliding symbol (or clear them)
    for(auto& oldSymbol : oldSymbols) {
      oldSymbol.first = std::move(oldSymbol.second);
    }

    return evaluatedPtr;
  }

private:
  std::vector<Symbol> m_parameters;

  static typename CompoundBatch::BatchList
  createCompountBatchList(CompoundBatch const& definitionBatch) {
    typename CompoundBatch::BatchList list;
    list.emplace_back(std::move(definitionBatch.clone()));
    return list;
  }
};

} // namespace boss::engines::bulk