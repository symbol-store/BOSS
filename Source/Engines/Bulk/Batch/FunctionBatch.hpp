#pragma once

#include "../SymbolPool.hpp"

namespace boss::engines::bulk {

class FunctionBatch : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<FunctionBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  using ParameterList = std::vector<Symbol>;

  FunctionBatch(BatchFactory const& factory, ParameterList const& parameters,
                ReadablePtr&& definitionBatchPtr)
      : m_parameters(parameters), CompoundBatch(factory, Symbol("Function"), true) {
    insert(0, 0, std::move(definitionBatchPtr));
  }

  FunctionBatch(BatchFactory const& factory, ParameterList&& parameters,
                ReadablePtr&& definitionBatchPtr)
      : m_parameters(std::move(parameters)), CompoundBatch(factory, Symbol("Function"), true) {
    insert(0, 0, std::move(definitionBatchPtr));
  }

  FunctionBatch(FunctionBatch const& other, bool clear = false)
      : m_parameters(other.m_parameters), CompoundBatch(other, clear) {}

  ~FunctionBatch() override = default;
  FunctionBatch(FunctionBatch&& other) = delete;
  FunctionBatch& operator=(FunctionBatch const& other) = delete;
  FunctionBatch& operator=(FunctionBatch&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsFunctionBatch(clear));
  }
  WritableBatchPtr<CompoundBatch> cloneAsCompoundBatch(bool clear = false) const override {
    return WritableBatchPtr<CompoundBatch>(cloneAsFunctionBatch(clear));
  }
  virtual WritableBatchPtr<FunctionBatch> cloneAsFunctionBatch(bool clear = false) const {
    return WritableBatchPtr(new FunctionBatch(*this, clear));
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, FunctionBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsFunctionBatch(clear);
  }

  Batch::ReadablePtr evaluateWith(std::vector<Batch const*> const& args) const {
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

    Batch::ReadablePtr evaluatedPtr;
    auto candidatePtr = *begin();
    while(true) {
      evaluatedPtr = std::move(candidatePtr);
      if(!evaluatedPtr->evaluate(candidatePtr)) {
        break;
      }
    }

    // before finishing, set back any colliding symbol (or clear them)
    for(auto& oldSymbol : oldSymbols) {
      oldSymbol.first = std::move(oldSymbol.second);
    }

    return evaluatedPtr;
  }

  bool evaluate(ReadablePtr& outputPtr) const override {
    // evaluate only by calling evaluateWith()
    outputPtr.reset();
    return false;
  }

  void merge(ReadablePtr&& other) override {
    CompoundBatch::merge<FunctionBatch>(std::move(other));
  }

private:
  ParameterList m_parameters;
};

} // namespace boss::engines::bulk