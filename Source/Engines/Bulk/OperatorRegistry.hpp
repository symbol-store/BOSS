#pragma once

#include "Batch/SymbolBatch.hpp"
#include "BatchVisitDispatcher.hpp"
#include "Operator.hpp"

#include "../../Expression.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace boss::engines::bulk {

/** This class is keeping a map of operators to be called for evaluating a complex expression.
 * They are stored by expression head and the number of arguments.
 * It needs to take the Executor as template parameter to be able to call back the execute()
 * function with a specific type of Operator, so the Operator are free to define any evaluate(...)
 * functions, templated or overloaded, to match the type of arguments that takes the operator.
 */
template <typename Executor> class OperatorRegistry {
private:
  OperatorRegistry() = default;

public:
  ~OperatorRegistry() = default;
  OperatorRegistry(OperatorRegistry const& other) = delete;
  OperatorRegistry(OperatorRegistry&& other) = delete;
  OperatorRegistry& operator=(OperatorRegistry const& other) = delete;
  OperatorRegistry& operator=(OperatorRegistry&& other) = delete;

  static OperatorRegistry& instance() {
    static OperatorRegistry instance;
    return instance;
  }

  void clear() { operators.clear(); }

  bool findAndExecuteOperator(Batch const& batch, Batch::ReadablePtr& outputPtr) const {
    bool handled = false;
    bool evaluated = false;
    BatchVisitDispatcher<CompoundBatch>::visit(
        [this, &handled, &evaluated, &batch, &outputPtr](auto const& compoundBatch) {
          handled = true;
          if(compoundBatch.getHead().getName() == "List") {
            // still need a special case here, until we handled it as an operator
            evaluated = executeCompoundBatchList(compoundBatch, outputPtr);
            return;
          }
          auto* container =
              findOperatorContainer(compoundBatch.getHead(), compoundBatch.numArguments());
          if(container != nullptr) {
            evaluated = container->execute(compoundBatch, outputPtr);
          }
        },
        batch);
    if(!handled) {
      // fallback to legacy method (for ValueBatch and SymbolBatch)
      return batch.evaluate(outputPtr);
    }
    return evaluated;
  }

  template <typename OperatorType> void registerOperator(std::string const& symbol) {
    using Properties = typename OperatorType::Properties;
    using ContainerType = OperatorContainer<OperatorType>;
    std::vector<size_t> argumentTypes(Properties::ArgumentCount, 0);
    OperatorKey key(symbol, argumentTypes);
    auto* container = new ContainerType(OperatorType());
    operators.try_emplace(key, container);
  }

private:
  bool executeCompoundBatchList(CompoundBatch const& batch, Batch::ReadablePtr& outputPtr) const {
    // set the local tuple to be accessible by the row values
    auto& symbolPtr = DefaultSymbolRegistry::instance().findSymbol(Symbol("$tuple"));
    auto backupSymbol = std::move(symbolPtr);
    symbolPtr = Batch::ReadablePtr(batch.shared_from_this());

    bool anyEvaluated = false;

    std::vector<Batch::ReadablePtr> argBatches;
    argBatches.reserve(batch.numArguments());
    for(auto const& argBatchPtr : batch) {
      Batch::ReadablePtr evaluatedPtr;
      if(!findAndExecuteOperator(*argBatchPtr, evaluatedPtr)) {
        argBatches.emplace_back(argBatchPtr);
      } else {
        argBatches.emplace_back(evaluatedPtr);
        anyEvaluated = true;
      }
    }

    // reset to any previous local tuple symbol
    symbolPtr = std::move(backupSymbol);

    if(!anyEvaluated) {
      outputPtr.reset();
      return false;
    }

    auto* newCompoundBatch = batch.cloneAsCompoundBatch(true);
    newCompoundBatch->append(argBatches);
    outputPtr = WritableBatchPtr(newCompoundBatch);
    return true;
  }

  class OperatorKey {
  public:
    OperatorKey(std::string const& symbol, std::vector<size_t> const& argumentTypes)
        : symbol(symbol), argumentTypes(argumentTypes) {}
    OperatorKey(std::string const& symbol, std::vector<size_t>&& argumentTypes)
        : symbol(symbol), argumentTypes(std::move(argumentTypes)) {}

    std::string const& getKey() const { return symbol; }

    size_t getArgumentCount() const { return argumentTypes.size(); }

    bool operator<(const OperatorKey& rhs) const {
      if(symbol != rhs.symbol) {
        return symbol < rhs.symbol;
      }

      if(argumentTypes.size() != rhs.argumentTypes.size()) {
        return argumentTypes.size() < rhs.argumentTypes.size();
      }

      for(int i = 0; i < argumentTypes.size(); ++i) {
        if(argumentTypes[i] != rhs.argumentTypes[i]) {
          return argumentTypes[i] < rhs.argumentTypes[i];
        }
      }
      return false;
    }

  private:
    std::string symbol;
    std::vector<size_t> argumentTypes;
  };

  class OperatorContainerBase {
  public:
    OperatorContainerBase() = default;
    virtual ~OperatorContainerBase() = default;
    OperatorContainerBase(OperatorContainerBase const& other) = default;
    OperatorContainerBase(OperatorContainerBase&& other) noexcept = default;
    OperatorContainerBase& operator=(OperatorContainerBase const& other) = default;
    OperatorContainerBase& operator=(OperatorContainerBase&& other) noexcept = default;

    virtual bool execute(CompoundBatch const& batch, Batch::ReadablePtr& outputPtr) const = 0;
  };

  template <typename OperatorType> class OperatorContainer : public OperatorContainerBase {
  public:
    explicit OperatorContainer(OperatorType const& op_) : op(op_) {}
    explicit OperatorContainer(OperatorType&& op_) : op(std::move(op_)) {}

    bool execute(CompoundBatch const& batch, Batch::ReadablePtr& outputPtr) const override {
      return Executor::execute(outputPtr, op, batch);
    }

  private:
    OperatorType op;
  };

  std::map<OperatorKey, std::unique_ptr<OperatorContainerBase>> operators;

  OperatorContainerBase* findOperatorContainer(Symbol const& symbol, size_t numArgs) const {
    std::vector<size_t> argumentTypes(numArgs, 0);
    OperatorKey key(symbol.getName(), argumentTypes);
    auto operatorIt = operators.find(key);
    if(operatorIt != operators.end()) {
      return operatorIt->second.get();
    }

    // Special case for binary operators: we can split arguments into pairs
    // to treat a longer argument list as a deeper compound expression.
    // This is needed because the arguments of the evaluation cannot be variadic
    // if we want them to be defined by the Operator at compile time.
    // We can get rid of it once the evaluation is not a lambda function anymore
    if(numArgs > 1) {
      // try to find a function with less arguments
      // and expect the compound batch to be able to handle that
      // [https://github.com/symbol-store/BOSS/issues/95] ideally, only if allowed on this operator
      auto closestOperatorIt = operators.lower_bound(key);
      if(closestOperatorIt != operators.end() && closestOperatorIt != operators.begin()) {
        --closestOperatorIt;
        if(closestOperatorIt->first.getKey() == symbol.getName()) {
          if(closestOperatorIt->first.getArgumentCount() == 2) {
            // create a compound expression batch
            // (the special case will be handled at evaluation time)
            return closestOperatorIt->second.get();
          }
        }
      }
    }

    return nullptr;
  }
};

} // namespace boss::engines::bulk
