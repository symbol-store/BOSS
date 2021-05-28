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

/// Helper to define a set of batch types allowed on one of the argument
/// (when allowing more than one type).
/// Used for calling to registerFunction
template <typename... BatchTypes> class AllowedBatches {
public:
  using BatchVisitDispatcher = BatchVisitDispatcher<BatchTypes...>;
  static bool isAllowed(Batch const& batch) {
    return (... || (batch.typeId() == UniqueId::forType<BatchTypes>()));
  }
};

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

  /** Wrapper for registerFunction
   * so we can set pass along some properties/flags for the operator
   * using template parameters.
   * At the moment, it is used to define the type of the arguments.*/
  friend class AllowedTypes;
  template <typename... Types> class AllowedTypes {
  public:
    explicit AllowedTypes(OperatorRegistry& registry_) : registry(registry_) {}

    /** This is the function to call to define a new operator
     * by passing a lambda function taking generic parameters.
     * The AllowedTypes wrapper is used to define types of the arguments,
     * and it will be resolve at compile-time to pass typed batch to the lambda function.*/
    template <size_t N, typename Func>
    void registerFunction(std::string const& symbol, Func&& func) {
      std::vector<size_t> argumentTypes(N, 0);
      OperatorKey key(symbol, argumentTypes);
      using OperatorType = RegisteredOperator<Func, N, Types...>;
      using ContainerType = OperatorContainer<OperatorType>;
      auto* container = new ContainerType(OperatorType(std::forward<Func>(func)));
      registry.operators.try_emplace(key, container);
    }

  private:
    OperatorRegistry& registry;
  };

  template <typename... Types> auto allowedTypes() {
    return FromElementTypesToAllowedTypes<false, Types...>(*this);
  }
  template <typename... Types> auto argTypes() {
    return FromElementTypesToAllowedTypes<true, Types...>(*this);
  }
  template <typename... Types> auto allowedBatchTypes() {
    return AllowedTypes<AllowedBatches<Types...>>(*this);
  }
  template <typename... Types> auto argBatchTypes() {
    return AllowedTypes<AsAllowedBatches<Types>...>(*this);
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

  template <typename> struct IsAllowedBatches : std::false_type {};
  template <typename... T> struct IsAllowedBatches<AllowedBatches<T...>> : std::true_type {};
  template <typename T>
  using AsAllowedBatches = std::conditional_t<IsAllowedBatches<T>::value, T, AllowedBatches<T>>;
  template <typename, typename> struct MergeTwoFixedTypes;
  template <typename... Args0, typename... Args1>
  struct MergeTwoFixedTypes<AllowedTypes<AllowedBatches<Args0...>>,
                            AllowedTypes<AllowedBatches<Args1...>>> {
    using type = AllowedTypes<AllowedBatches<Args0...>, AllowedBatches<Args1...>>;
  };
  template <typename, typename> struct MergeTwoAllowedTypes;
  template <typename... Args0, typename... Args1>
  struct MergeTwoAllowedTypes<AllowedTypes<AllowedBatches<Args0...>>,
                              AllowedTypes<AllowedBatches<Args1...>>> {
    using type = AllowedTypes<AllowedBatches<Args0..., Args1...>>;
  };
  template <bool, typename...> struct MergeAllowedTypes;
  template <bool UsingFixedTypes, typename FirstAllowedType>
  struct MergeAllowedTypes<UsingFixedTypes, FirstAllowedType> {
    using type = FirstAllowedType;
  };
  template <bool UsingFixedTypes, typename FirstAllowedType, typename... OtherAllowedTypes>
  struct MergeAllowedTypes<UsingFixedTypes, FirstAllowedType, OtherAllowedTypes...> {
    using type = std::conditional_t<
        UsingFixedTypes,
        typename MergeTwoFixedTypes<
            FirstAllowedType,
            typename MergeAllowedTypes<UsingFixedTypes, OtherAllowedTypes...>::type>::type,
        typename MergeTwoAllowedTypes<
            FirstAllowedType,
            typename MergeAllowedTypes<UsingFixedTypes, OtherAllowedTypes...>::type>::type>;
  };
  template <bool UsingFixedTypes, typename... Types>
  using FromElementTypesToAllowedTypes = typename MergeAllowedTypes<
      UsingFixedTypes,
      std::conditional_t<std::is_same_v<Types, Symbol>, AllowedTypes<AllowedBatches<SymbolBatch>>,
                         std::conditional_t<std::is_same_v<Types, ComplexExpression>,
                                            AllowedTypes<AllowedBatches<CompoundBatch>>,
                                            AllowedTypes<AllowedBatches<ValueBatch<Types>>>>>...>::
      type;

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
