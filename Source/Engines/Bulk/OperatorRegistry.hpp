#pragma once

#include "Batch/SymbolBatch.hpp"
#include "BatchVisitDispatcher.hpp"
#include "Operator.hpp"
#include "SymbolRegistry.hpp"

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

  bool findAndExecuteOperator(BulkExpression const& expression, BulkExpression& output) const {
    bool evaluated = false;
    if(std::holds_alternative<BulkComplexExpression>(expression)) {
      auto const& complexExpression = std::get<BulkComplexExpression>(expression);
      auto const& head = complexExpression.getHead();
      auto numArgs = complexExpression.getArguments().size();
      auto* container = findOperatorContainer(head, numArgs);
      if(container != nullptr) {
        evaluated = container->execute(complexExpression, output);
      } else {
        // at least evaluate arguments and return as is
        BulkExpressionArguments evaluatedArgs;
        evaluatedArgs.reserve(numArgs);
        BulkExpression evaluatedArg;
        for(auto const& arg : complexExpression.getArguments()) {
          if(findAndExecuteOperator(arg, evaluatedArg)) {
            evaluated = true;
            evaluatedArgs.emplace_back(std::move(evaluatedArg));
          } else {
            evaluatedArgs.emplace_back(arg);
          }
        }
        output = BulkComplexExpression(head, std::move(evaluatedArgs));
      }
    } else if(std::holds_alternative<Symbol>(expression)) {
      auto const& symbol = std::get<Symbol>(expression);
      auto const& batchPtr = DefaultSymbolRegistry::instance().findSymbol(symbol);
      if(batchPtr) {
        output = *batchPtr;
        evaluated = true;
      }
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

    virtual bool execute(BulkComplexExpression const& expression, BulkExpression& output) const = 0;
  };

  template <typename OperatorType> class OperatorContainer : public OperatorContainerBase {
  public:
    explicit OperatorContainer(OperatorType const& op_) : op(op_) {}
    explicit OperatorContainer(OperatorType&& op_) : op(std::move(op_)) {}

    bool execute(BulkComplexExpression const& expression, BulkExpression& output) const override {
      return Executor::execute(output, op, expression);
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
