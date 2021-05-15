#pragma once

#include "BatchFactory.hpp"
#include "Evaluator.hpp"
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
template <typename... SupportedTypes> class BatchTemplates : public BatchFactory {
public:
  using CompoundBatchHelper =
      BatchHelper<CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  using BatchHelper = BatchHelper<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch,
                                  CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  using AnyBatch = AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch,
                                  CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  using NonSymbolicBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, CompoundBatch, TableView>;

  using AnySimpleBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., ValueBatch<Symbol>, SymbolBatch>;

  using AnyCompoundBatch =
      AllowedBatches<CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  BatchTemplates() {
    // [ISSUE] get rid of this special case
    BatchTemplateKey unevaluatedKey("Unevaluated", std::vector<size_t>(1, 0));
    m_templates[unevaluatedKey] =
        BatchTemplatePtr(new DeferredEvaluationBatchTemplate("Unevaluated"));
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
    newBatch->insert(expression);
    return newBatch;
  }

  Batch* createBatch(Symbol const& symbol, size_t numArgs) const {
    std::vector<size_t> argumentTypes(numArgs, 0);
    BatchTemplateKey key(symbol.getName(), argumentTypes);
    auto templateIt = m_templates.find(key);
    if(templateIt != m_templates.end()) {
      auto* batchTemplate = templateIt->second.get();
      return batchTemplate->createBatch(*this);
    }

    // Special case for binary operators: we can split arguments into pairs
    // to treat a longer argument list as a deeper compound expression.
    // This is needed because the evaluator arguments cannot be variadic
    // if we want them to be defined by the ExpressionBatch at compile time.
    if(numArgs > 1) {
      // try to find a function with less arguments
      // // and expect the compound batch to be able to handle that
      // [ISSUE] only if variadic is allowed on these functions
      auto closestTemplateIt = m_templates.lower_bound(key);
      if(closestTemplateIt != m_templates.end() && closestTemplateIt != m_templates.begin()) {
        --closestTemplateIt;
        auto* batchTemplate = closestTemplateIt->second.get();
        if(closestTemplateIt->first.getKey() == symbol.getName()) {
          if(closestTemplateIt->first.getArgumentCount() == 2) {
            // create a compound expression batch
            // (the special case will be handled at evaluation time)
            return batchTemplate->createBatch(*this);
          }
        }
      }
    }

    // symbol not found, return a generic batch with arguments at least
    return new CompoundBatch(*this, symbol);
  }

  template <typename T> Batch* createBatch(T const& value) const {
    return new ValueBatch<T>(1, value);
  }

  /// consume a pair of (array vector, builder) to create a batch from them
  Batch* createBatch(arrow::ArrayVector&& arrays,
                     std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder) const override {
    // assuming all arrays and builder share the same type!
    auto type = arrayBuilder ? arrayBuilder->type() : arrays[0]->type();

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
        return new SymbolBatch(*this, std::move(arrays), std::move(arrayBuilder));
      }
      // COMPLEX EXPRESSION
      auto const& complexType =
          dynamic_cast<ComplexExpressionArray::ComplexExpressionArrayType const&>(extensionType);
      auto const& head = complexType.getHead();
      auto* batchPtr = createBatch(head, extensionType.storage_type()->num_fields());
      CompoundBatchHelper::visit(
          [&arrays, &arrayBuilder](auto& batch) {
            batch.insert(CompoundArray(std::move(arrays), std::move(arrayBuilder)));
          },
          *batchPtr);
      return batchPtr;
    }
    default:
      break;
    }

    // [ISSUE] handle exceptions. with a pre-processor flag if affects performance.
    return nullptr; // should not happen!
  }

  /// convert the batch back to an expression
  Expression revertToExpression(Batch::ReadablePtr&& batchPtr) const override {
    if(batchPtr->typeId() == UniqueId::forType<TableView>()) {
      // save the query result into a temporary symbol
      // this is a workaround to avoid a whole table to be converted back
      // to a long list of tuples
      // [ISSUE] (symbol pool symbols lifetime handling) find a way to garbage-collect them
      static int i = 0;
      auto symbolName = "_table" + std::to_string(i++);
      auto writablePtr = Batch::WritablePtr::asWritable(batchPtr);
      boss::engines::bulk::BatchHelper<TableView>::visit(
          [&symbolName](auto& tableView) {
            auto numRows = tableView.size();
            auto numCols = tableView.numColumns();
            symbolName += "_cols" + std::to_string(numCols) + "rows" + std::to_string(numRows);
          },
          *writablePtr);
      Symbol savedSymbol(symbolName);
      auto& savedSymbolPtr = DefaultSymbolPool::instance().findSymbol(savedSymbol);
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
    BatchHelper::visit(
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
    explicit AllowedTypes(BatchTemplates& batchTemplates) : m_batchTemplates(batchTemplates) {}

    /** This is the function to call to define a new oeprator
     * by passing a lambda function taking generic paremeters.
     * The AllowedTypes wrapper is used to define types of the arguments,
     * and it will be resolve at compile-time to pass typed batch to the lambda function.*/
    template <size_t N, typename Func>
    void registerFunction(std::string const& symbol, Func&& func) {
      std::vector<size_t> argumentTypes(N, 0);
      // [ISSUE] nto handling overloading yet
      /*
        // support overloading only when specify every argument type
        if constexpr(IsBatchType) {
          argumentTypes = std::vector<size_t>{((size_t)UniqueId::forType<typename
        Types::ValueType>())...}; } else { argumentTypes =
        std::vector<size_t>{((size_t)UniqueId::forType<Types>())...};
      }*/
      BatchTemplateKey key(symbol, argumentTypes);
      auto* templateBatch =
          new ExpressionBatchTemplate<Func, N, Types...>(symbol, std::forward<Func>(func));
      m_batchTemplates.m_templates[key] = BatchTemplatePtr(templateBatch);
    }

  private:
    BatchTemplates& m_batchTemplates;
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

  // [ISSUE] factor this out (using the already existing CompareExpression class?)
  // This is the class used to map an operator signature (head + arguments)
  // to the batch templates (and so the evaluators)
  class BatchTemplateKey {
  public:
    BatchTemplateKey(std::string const& symbol, std::vector<size_t> const& argumentTypes)
        : m_symbol(symbol), m_argumentTypes(argumentTypes) {}
    BatchTemplateKey(std::string const& symbol, std::vector<size_t>&& argumentTypes)
        : m_symbol(symbol), m_argumentTypes(std::move(argumentTypes)) {}

    std::string const& getKey() const { return m_symbol; }

    size_t getArgumentCount() const { return m_argumentTypes.size(); }

    bool operator<(const BatchTemplateKey& rhs) const {
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

  class BatchTemplateBase {
  public:
    virtual ~BatchTemplateBase() = default;
    BatchTemplateBase() = default;
    BatchTemplateBase(BatchTemplateBase const& other) = delete;
    BatchTemplateBase(BatchTemplateBase&& other) = delete;
    BatchTemplateBase& operator=(BatchTemplateBase const& other) = delete;
    BatchTemplateBase& operator=(BatchTemplateBase&& other) = delete;

    virtual Batch* createBatch(BatchTemplates const&) const = 0;
  };

  class DeferredEvaluationBatchTemplate : public BatchTemplateBase {
  public:
    explicit DeferredEvaluationBatchTemplate(std::string const& symbolName)
        : m_symbolName(symbolName) {}

    Batch* createBatch(BatchTemplates const& templates) const override {
      return new DeferredEvaluationBatch(templates, Symbol(m_symbolName));
    }

  private:
    std::string const m_symbolName;
  };

  template <typename Func, int N, typename... AllowedTypes>
  class ExpressionBatchTemplate : public BatchTemplateBase {
  public:
    using EvaluatorType = typename ForTypes<AllowedTypes...>::template Evaluator<Func>;
    using BatchType = ExpressionBatch<EvaluatorType, Func, N>;

    ExpressionBatchTemplate(std::string const& symbol, Func&& func)
        : m_evaluator(symbol, std::forward<Func>(func)) {}

    Batch* createBatch(BatchTemplates const& templates) const override {
      return new BatchType(templates, m_evaluator);
    }

  private:
    EvaluatorType m_evaluator;
  };

  using BatchTemplatePtr = std::unique_ptr<BatchTemplateBase>;
  std::map<BatchTemplateKey, BatchTemplatePtr> m_templates;
};

} // namespace boss::engines::bulk
