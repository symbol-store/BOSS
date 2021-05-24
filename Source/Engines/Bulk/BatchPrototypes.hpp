#pragma once

#include "BatchFactory.hpp"
#include "Evaluator.hpp"
#include "SymbolRegistry.hpp"
#include "TableView.hpp"

#include "Batch/Batch.hpp"
#include "Batch/CompoundBatch.hpp"
#include "Batch/ExpressionBatch.hpp"
#include "Batch/FunctionBatch.hpp"
#include "Batch/SymbolBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include "../../Expression.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace boss::engines::bulk {

/** This class is the implementation of the BatchFactory interface.
 For simple data types, it is just a straightforward creation of the matching ValueBatch type.
 For complex expression, it has to create an ExpressionBatch containing the evaluator matching the
 operator signature (head and arguments).
 To do so, we expose a registerFunction method for the user to create evaluators from lambda
 functions. Those evaluators are stored into Batch prototypes. Then, when calling CreateBatch, we
 retrieve the prototype using a map from the operator signature, and the prototype can create the
 ExpressionBatch by passing over the stored evaluator. */
template <typename... SupportedTypes> class BatchPrototypes : public BatchFactory {
public:
  using CompoundBatchDispatcher =
      BatchVisitDispatcher<CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  using BatchVisitDispatcher =
      BatchVisitDispatcher<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch,
                           CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  using AnyBatch = AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch,
                                  CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  using NonSymbolicBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, CompoundBatch, TableView>;

  using AnySimpleBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch>;

  using AnyCompoundBatch =
      AllowedBatches<CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  BatchPrototypes() {
    // [https://github.com/symbol-store/BOSS/issues/85] get rid of this special case
    BatchPrototypeKey unevaluatedKey("Unevaluated", std::vector<size_t>(1, 0));
    m_prototypes[unevaluatedKey] =
        BatchPrototypePtr(new DeferredEvaluationBatchPrototype("Unevaluated"));
  }

  Batch* createBatch(Expression const& expression) const override {
    return std::visit([this](auto&& value) { return this->createBatch(value); }, expression);
  }

  Batch* createBatch(ComplexExpression const& expression) const {
    auto const& symbol = expression.getHead();
    auto argsBegin = expression.getArguments().begin();
    auto argsEnd = expression.getArguments().end();
    size_t numArgs = expression.getArguments().size();
    auto* newBatch = createBatch(symbol, numArgs);
    // I think, in general we can inline a lot of things here
    newBatch->append(expression);
    return newBatch;
  }

  Batch* createBatch(Symbol const& symbol, size_t numArgs) const {
    std::vector<size_t> argumentTypes(numArgs, 0);
    BatchPrototypeKey key(symbol.getName(), argumentTypes);
    auto prototypeIt = m_prototypes.find(key);
    if(prototypeIt != m_prototypes.end()) {
      auto* batchPrototype = prototypeIt->second.get();
      return batchPrototype->createBatch();
    }

    // Special case for binary operators: we can split arguments into pairs
    // to treat a longer argument list as a deeper compound expression.
    // This is needed because the evaluator arguments cannot be variadic
    // if we want them to be defined by the ExpressionBatch at compile time.
    if(numArgs > 1) {
      // try to find a function with less arguments
      // and expect the compound batch to be able to handle that
      // [https://github.com/symbol-store/BOSS/issues/95] ideally, only if allowed on this operator
      auto closestPrototypeIt = m_prototypes.lower_bound(key);
      if(closestPrototypeIt != m_prototypes.end() && closestPrototypeIt != m_prototypes.begin()) {
        --closestPrototypeIt;
        auto* batchPrototype = closestPrototypeIt->second.get();
        if(closestPrototypeIt->first.getKey() == symbol.getName()) {
          if(closestPrototypeIt->first.getArgumentCount() == 2) {
            // create a compound expression batch
            // (the special case will be handled at evaluation time)
            return batchPrototype->createBatch();
          }
        }
      }
    }

    // symbol not found, return a generic batch with arguments at least
    return new CompoundBatch(symbol);
  }

  template <typename T> Batch* createBatch(T const& value) const {
    return new ValueBatch<T>(1, value);
  }

  /// consume a pair of (array vector, builder) to create a batch from them
  Batch* createBatch(arrow::ArrayVector&& arrays,
                     std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder) const override {
    auto type = arrayBuilder ? arrayBuilder->type() : arrays[0]->type();
    // assuming all arrays and builder share the same type!
    // if not, keep only the latest type
    auto arrayIt = arrays.rbegin();
    for(; arrayIt != arrays.rend(); ++arrayIt) {
      if((*arrayIt)->type_id() != type->id()) {
        break;
      }
    }
    if(arrayIt != arrays.rend()) {
      // shrinked without the arrays having a different type
      arrow::ArrayVector shrinkedArrays(arrayIt.base(), arrays.end());
      arrays.swap(shrinkedArrays);
    }

    switch(type->id()) {
    case arrow::Type::BOOL:
      return new ValueBatch<bool>(std::move(arrays), std::move(arrayBuilder));
    case arrow::Type::INT32:
      return new ValueBatch<int>(std::move(arrays), std::move(arrayBuilder));
    case arrow::Type::FLOAT:
      return new ValueBatch<float>(std::move(arrays), std::move(arrayBuilder));
    case arrow::Type::STRING:
      return new ValueBatch<std::string>(std::move(arrays), std::move(arrayBuilder));
    case arrow::Type::EXTENSION: {
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      if(extensionType.extension_name()[0] == 's') {
        // SYMBOL
        return new SymbolBatch(std::move(arrays), std::move(arrayBuilder));
      }
      // COMPLEX EXPRESSION
      auto const& complexType =
          dynamic_cast<ComplexExpressionArray::ComplexExpressionArrayType const&>(extensionType);
      auto const& head = complexType.getHead();
      auto* batchPtr = createBatch(head, extensionType.storage_type()->num_fields());
      CompoundBatchDispatcher::visit(
          [&arrays, &arrayBuilder](auto& batch) {
            batch.append(CompoundArray(std::move(arrays), std::move(arrayBuilder)));
          },
          *batchPtr);
      return batchPtr;
    }
    default:
      break;
    }

    // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    return nullptr; // should not happen!
  }

  /// convert the batch back to an expression
  Expression revertToExpression(Batch::ReadablePtr&& batchPtr) const override {
    if(batchPtr->typeId() == UniqueId::forType<TableView>()) {
      // save the query result into a temporary symbol
      // this is a workaround to avoid a whole table to be converted back
      // to a long list of tuples
      // [https://github.com/symbol-store/BOSS/issues/91] find a way to garbage-collect them
      static int i = 0;
      auto symbolName = "_table" + std::to_string(i++);
      auto writablePtr = Batch::WritablePtr::asWritable(batchPtr);
      boss::engines::bulk::BatchVisitDispatcher<TableView>::visit(
          [&symbolName](auto& tableView) {
            auto numRows = tableView.size();
            auto numCols = tableView.numColumns();
            symbolName += "_cols" + std::to_string(numCols) + "rows" + std::to_string(numRows);
          },
          *writablePtr);
      Symbol savedSymbol(symbolName);
      auto& savedSymbolPtr = DefaultSymbolRegistry::instance().findSymbol(savedSymbol);
      savedSymbolPtr = std::move(batchPtr);
      return savedSymbol;
    }

    // evaluate now any remaining "unevaluated" values on extraction
    // or would it be better to let the user do that manually?
    // TODO: is it still needed?
    Batch::ReadablePtr evaluatedPtr;
    if(batchPtr->evaluate(evaluatedPtr)) {
      // (but keep it unevaluated for a TableView's symbol)
      if(evaluatedPtr->typeId() != UniqueId::forType<TableView>()) {
        batchPtr = std::move(evaluatedPtr);
      }
    }

    auto const& batch = *batchPtr;
    std::optional<Symbol> rootHead;
    ExpressionArguments arguments;
    arguments.reserve(batch.size());
    BatchVisitDispatcher::visit(
        [this, &arguments, &rootHead, batchPtr{std::move(batchPtr)}](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            rootHead = batch.getHead();
            size_t batchSize = batch.size();
            for(size_t index = 0; index < batchSize; ++index) {
              auto extractedPtr = batch.extract(index);
              arguments.emplace_back(revertToExpression(std::move(extractedPtr)));
            }
          } else {
            for(auto const& value : batch) {
              arguments.emplace_back(static_cast<typename BatchType::ValueType>(value));
            }
          }
        },
        batch);

    if(arguments.size() == 1 && !rootHead) {
      return arguments[0];
    }

    Symbol const& head = rootHead ? *rootHead : Symbol("List");
    return ComplexExpression(head, arguments);
  }

  /** Wrapper for registerFunction
   * so we can set pass along some properties/flags for the evaluator
   * using template parameters.
   * At the moment, it is used to define the type of the arguments.*/
  friend class AllowedTypes;
  template <typename... Types> class AllowedTypes {
  public:
    explicit AllowedTypes(BatchPrototypes& prototypes) : m_batchPrototypes(prototypes) {}

    /** This is the function to call to define a new operator
     * by passing a lambda function taking generic parameters.
     * The AllowedTypes wrapper is used to define types of the arguments,
     * and it will be resolve at compile-time to pass typed batch to the lambda function.*/
    template <size_t N, typename Func>
    void registerFunction(std::string const& symbol, Func&& func) {
      std::vector<size_t> argumentTypes(N, 0);
      // [https://github.com/symbol-store/BOSS/issues/96] not handling overloading yet
      /*
        // support overloading only when specify every argument type
        if constexpr(IsBatchType) {
          argumentTypes = std::vector<size_t>{((size_t)UniqueId::forType<typename
        Types::ValueType>())...}; } else { argumentTypes =
        std::vector<size_t>{((size_t)UniqueId::forType<Types>())...};
      }*/
      BatchPrototypeKey key(symbol, argumentTypes);
      auto* prototypeBatch =
          new ExpressionBatchPrototype<Func, N, Types...>(symbol, std::forward<Func>(func));
      m_batchPrototypes.m_prototypes[key] = BatchPrototypePtr(prototypeBatch);
    }

  private:
    BatchPrototypes& m_batchPrototypes;
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
  /////////////////////////////////////////////////////////////
  // [https://github.com/symbol-store/BOSS/issues/94] to refactor
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
  /////////////////////////////////////////////////////////////

  // [https://github.com/symbol-store/BOSS/issues/93] to refactor
  // This is the class used to map an operator signature (head + arguments)
  // to the batch prototypes (and so the evaluators)
  class BatchPrototypeKey {
  public:
    BatchPrototypeKey(std::string const& symbol, std::vector<size_t> const& argumentTypes)
        : m_symbol(symbol), m_argumentTypes(argumentTypes) {}
    BatchPrototypeKey(std::string const& symbol, std::vector<size_t>&& argumentTypes)
        : m_symbol(symbol), m_argumentTypes(std::move(argumentTypes)) {}

    std::string const& getKey() const { return m_symbol; }

    size_t getArgumentCount() const { return m_argumentTypes.size(); }

    bool operator<(const BatchPrototypeKey& rhs) const {
      if(m_symbol != rhs.m_symbol) {
        return m_symbol < rhs.m_symbol;
      }

      if(m_argumentTypes.size() != rhs.m_argumentTypes.size()) {
        return m_argumentTypes.size() < rhs.m_argumentTypes.size();
      }

      for(int i = 0; i < m_argumentTypes.size(); ++i) {
        if(m_argumentTypes[i] != rhs.m_argumentTypes[i]) {
          return m_argumentTypes[i] < rhs.m_argumentTypes[i];
        }
      }
      return false;
    }

  private:
    std::string m_symbol;
    std::vector<size_t> m_argumentTypes;
  };

  class BatchPrototypeBase {
  public:
    virtual ~BatchPrototypeBase() = default;
    BatchPrototypeBase() = default;
    BatchPrototypeBase(BatchPrototypeBase const& other) = delete;
    BatchPrototypeBase(BatchPrototypeBase&& other) = delete;
    BatchPrototypeBase& operator=(BatchPrototypeBase const& other) = delete;
    BatchPrototypeBase& operator=(BatchPrototypeBase&& other) = delete;

    virtual Batch* createBatch() const = 0;
  };

  class DeferredEvaluationBatchPrototype : public BatchPrototypeBase {
  public:
    explicit DeferredEvaluationBatchPrototype(std::string const& symbolName)
        : m_symbolName(symbolName) {}

    Batch* createBatch() const override {
      return new DeferredEvaluationBatch(Symbol(m_symbolName));
    }

  private:
    std::string const m_symbolName;
  };

  template <typename Func, int N, typename... AllowedTypes>
  class ExpressionBatchPrototype : public BatchPrototypeBase {
  public:
    using EvaluatorType = typename ForTypes<AllowedTypes...>::template Evaluator<Func>;
    using BatchType = ExpressionBatch<EvaluatorType, Func, N>;

    ExpressionBatchPrototype(std::string const& symbol, Func&& func)
        : m_evaluator(symbol, std::forward<Func>(func)) {}

    Batch* createBatch() const override { return new BatchType(m_evaluator); }

  private:
    EvaluatorType m_evaluator;
  };

  using BatchPrototypePtr = std::unique_ptr<BatchPrototypeBase>;
  std::map<BatchPrototypeKey, BatchPrototypePtr> m_prototypes;
};

} // namespace boss::engines::bulk
