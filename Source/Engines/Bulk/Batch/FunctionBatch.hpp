#pragma once

#include "../SymbolPool.hpp"

namespace boss::engines::bulk {

// [ISSUE] try to get rid of it.
// but still currently useful, at least to apply it as a type to other operators.
// start a discussion in the issue...
// from Holger's comment: I would generally advise to
// avoid coding out functionality as part of the core engine at the C++ level.
// Maybe we should think about an (object-oriented) extension interface
/** FunctionBatch is a special case of compound batch where the only child batch is the body0
 * We use it for queries requiring to pass a function (as predicate, custom aggregation, grouping
 * etc). Those queries can call evaluateWith, passing the relation as parameter (but could be
 * anything else too). We store the parameter as symbol, and then the body will be able to evaluate
 * and apply the symbol when body.evaluate() is called. */
class FunctionBatch : public CompoundBatch {
public:
  using ValueType = CompoundBatch::ValueType;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<FunctionBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  using ParameterList = std::vector<Symbol>;

  FunctionBatch(BatchFactory const& factory, ParameterList const& parameters,
                ReadablePtr&& definitionBatchPtr)
      : m_parameters(parameters), CompoundBatch(factory, Symbol("Function")) {
    insert(std::vector{std::move(definitionBatchPtr)});
  }

  FunctionBatch(BatchFactory const& factory, ParameterList&& parameters,
                ReadablePtr&& definitionBatchPtr)
      : m_parameters(std::move(parameters)), CompoundBatch(factory, Symbol("Function")) {
    insert(std::vector{std::move(definitionBatchPtr)});
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

  Batch::ReadablePtr evaluateWith(std::vector<Batch::ReadablePtr> const& args) const {
    auto candidatePtr = *begin(); // always single argument (the body)

    // replace parameter symbols with arguments
    std::vector<std::pair<DefaultSymbolPool::SymbolPtr&, DefaultSymbolPool::SymbolPtr>> oldSymbols;
    oldSymbols.reserve(args.size());
    auto paramIt = m_parameters.begin();
    for(auto arg : args) {
      if(paramIt == m_parameters.end()) {
        break;
      }

      // store existing symbols
      // to retrieve later in case of name collision
      // (and make sure they are not destroyed while dereferenced...)
      auto& batchPtr = DefaultSymbolPool::instance().findSymbol(Symbol(*paramIt));
      oldSymbols.emplace_back(batchPtr, std::move(batchPtr));

      // set symbol at the function scope
      batchPtr = std::move(arg);

      ++paramIt;
    }

    // evaluate as much as possible
    Batch::ReadablePtr evaluatedPtr;
    while(true) {
      evaluatedPtr = std::move(candidatePtr);
      if(!evaluatedPtr->evaluate(candidatePtr)) {
        // finished to evaluate
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
    // [ISSUE] how to generalise the evaluation of a FunctionBatch.
    // from Holger's comment: do you think we could remove this function and always call
    // evaluateWith but without parameters?

    // evaluate only by calling evaluateWith()
    outputPtr.reset();
    return false;
  }

private:
  ParameterList m_parameters;
};

} // namespace boss::engines::bulk
