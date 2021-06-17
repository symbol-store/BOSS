#pragma once

#include "SymbolRegistry.hpp"

#include "../../Expression.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace boss::engines::bulk {

/** This class is keeping a map of operators to be called for evaluating a complex expression.
 * They are stored by expression head and the number of parameters.
 * It needs to take the Executor as template parameter to be able to call back the execute()
 * function with a specific type of Operator.
 * This way, the Operator classes are free to define any evaluate(...) functions, even template or
 * overloaded, to match the type of arguments that the operator takes.
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

  BulkExpression findAndExecuteOperator(BulkExpression const& expression) const {
    return std::visit(
        utilities::overload(
            [this](BulkComplexExpression const& complexExpression) -> BulkExpression {
              auto const& head = complexExpression.getHead();
              auto const numArgs = complexExpression.getArguments().size();
              auto* container = findOperatorContainer(head, numArgs);
              if(container != nullptr) {
                return container->execute(complexExpression);
              }
              if(head.getName() == "List") {
                // if we cannot find an operator for this head,
                // at least evaluate arguments and return as is
                return evaluateArguments(complexExpression);
              }
              return complexExpression;
            },
            [this](Symbol const& symbol) {
              BulkComplexExpression symbolExpr(Symbol("Symbol"), {symbol.getName()});
              auto* container = findOperatorContainer(symbolExpr.getHead(), 1);
              return container != nullptr ? container->execute(symbolExpr) : symbol;
            },
            [this](std::shared_ptr<CompoundArray> const& compoundArrayPtr) -> BulkExpression {
              auto const& compoundArray = *compoundArrayPtr;
              if(compoundArray.isDecomposed()) {
                // we cannot turn it directly into an expression
                // without transposing the rows
                // so just keep it as a CompoundArray
                return compoundArrayPtr;
              }
              auto const& head = compoundArray.getHead();
              auto const numArgs = compoundArray.numArguments();
              auto* container = findOperatorContainer(head, numArgs);
              if(container == nullptr) {
                return compoundArrayPtr;
              }
              BulkExpressionArguments args;
              args.reserve(numArgs);
              for(auto&& arg : compoundArray) {
                args.emplace_back(std::move(arg));
              }
              return container->execute(BulkComplexExpression(head, args), &compoundArray);
            },
            [&expression](auto const& /*other*/) {
              // any other type: just return as is
              return expression;
            }),
        (BulkExpression::SuperType const&)expression);
  }

  /// return < 0 if the operator cannot be found
  int findOperatorAndGetNumParameters(Symbol const& head) const {
    auto* container = findOperatorContainer(head);
    if(container != nullptr) {
      return container->numParameters();
    }
    return -1;
  }

  template <typename OperatorType> void registerOperator(std::string const& symbol) {
    using Properties = typename OperatorType::Properties;
    using ContainerType = OperatorContainer<OperatorType>;
    OperatorKey key(symbol, Properties::ParameterCount);
    auto* container = new ContainerType(OperatorType());
    operators.try_emplace(key, container);
  }

private:
  BulkExpression evaluateArguments(BulkComplexExpression const& complexExpression) const {
    // set the local tuple to be accessible by the row values
    auto& symbolRegistry = DefaultSymbolRegistry::instance();
    auto& symbolPtr = symbolRegistry.findSymbol(Symbol("$tuple"));
    auto backupSymbol = std::move(symbolPtr);
    symbolRegistry.setSymbol(symbolPtr, complexExpression);

    // reconstruct a new expression with evaluated arguments
    BulkExpressionArguments evaluatedArgs;
    evaluatedArgs.reserve(complexExpression.getArguments().size());
    for(auto const& arg : complexExpression.getArguments()) {
      evaluatedArgs.emplace_back(findAndExecuteOperator(arg));
    }

    // reset to any previous local tuple symbol
    symbolPtr = std::move(backupSymbol);

    return BulkComplexExpression(complexExpression.getHead(), evaluatedArgs);
  }

  class OperatorContainerBase {
  public:
    OperatorContainerBase() = default;
    virtual ~OperatorContainerBase() = default;
    OperatorContainerBase(OperatorContainerBase const& other) = default;
    OperatorContainerBase(OperatorContainerBase&& other) noexcept = default;
    OperatorContainerBase& operator=(OperatorContainerBase const& other) = default;
    OperatorContainerBase& operator=(OperatorContainerBase&& other) noexcept = default;

    virtual unsigned int numParameters() const = 0;
    virtual BulkExpression execute(BulkComplexExpression const& expression,
                                   CompoundArray const* contextArray = nullptr) = 0;
  };

  template <typename OperatorType> class OperatorContainer : public OperatorContainerBase {
  public:
    explicit OperatorContainer(OperatorType const& op_) : op(op_) {}
    explicit OperatorContainer(OperatorType&& op_) : op(std::move(op_)) {}

    unsigned int numParameters() const override { return OperatorType::Properties::ParameterCount; }

    BulkExpression execute(BulkComplexExpression const& expression,
                           CompoundArray const* contextArray = nullptr) override {
      op.setContext(contextArray);
      auto result = Executor::execute(op, expression);
      op.clearContext();
      return result;
    }

  private:
    OperatorType op;
  };

  /// find an operator based on head + num parameters.
  /// - if an operator with exactly numArgs cannot be found,
  /// it will try to find the closest match < numArgs
  /// - if called without numArgs, it will try to find the operator with largest numArgs
  OperatorContainerBase* findOperatorContainer(Symbol const& symbol,
                                               size_t numArgs = size_t(-1)) const {
    OperatorKey key(symbol.getName(), numArgs);
    auto operatorIt = operators.find(key);
    if(operatorIt != operators.end()) {
      return operatorIt->second.get();
    }

    // try to find the closest operator with less prameters
    if(numArgs > 1) {
      auto closestOperatorIt = operators.lower_bound(key);
      if(closestOperatorIt != operators.end() && closestOperatorIt != operators.begin()) {
        --closestOperatorIt;
        if(closestOperatorIt->first.getKey() == symbol.getName()) {
          return closestOperatorIt->second.get();
        }
      }
    }

    return nullptr;
  }

  class OperatorKey {
  public:
    OperatorKey(std::string const& symbol, size_t parameterCount)
        : symbol(symbol), parameterCount(parameterCount) {}

    std::string const& getKey() const { return symbol; }

    size_t getParameterCount() const { return parameterCount; }

    bool operator<(const OperatorKey& rhs) const {
      if(symbol != rhs.symbol) {
        return symbol < rhs.symbol;
      }
      return parameterCount < rhs.parameterCount;
    }

  private:
    std::string symbol;
    size_t parameterCount;
  };

  std::map<OperatorKey, std::unique_ptr<OperatorContainerBase>> operators;
};

} // namespace boss::engines::bulk
