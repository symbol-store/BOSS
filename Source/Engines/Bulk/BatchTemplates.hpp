#pragma once

#include "BatchFactory.hpp"
#include "Evaluator.hpp"
#include "TableView.hpp"

#include "Batch/Batch.hpp"
#include "Batch/CompoundBatch.hpp"
#include "Batch/ExpressionBatch.hpp"
#include "Batch/FunctionBatch.hpp"
#include "Batch/RLEBatch.hpp"
#include "Batch/SymbolBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include "../../Expression.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace boss::engines::bulk {

/******************* class BatchTemplates *********************/

/* keep a map of Evaluators for each symbol                   */
/* then createBatch can create the right ExpressionBatch      */
/* for any complex expression                                 */
/**************************************************************/

template <typename... SupportedTypes> class BatchTemplates : public BatchFactory {
public:
  using CompoundBatchHelper =
      BatchHelper<CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;
  using BatchHelper =
      BatchHelper<ValueBatch<SupportedTypes>..., RLEBatch<SupportedTypes>..., SymbolBatch,
                  CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  using AnyBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., RLEBatch<SupportedTypes>..., SymbolBatch,
                     CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;
  using NonSymbolicBatch = AllowedBatches<ValueBatch<SupportedTypes>...,
                                          RLEBatch<SupportedTypes>..., CompoundBatch, TableView>;

  using AnySimpleBatch =
      AllowedBatches<ValueBatch<SupportedTypes>..., RLEBatch<SupportedTypes>..., SymbolBatch>;

  using AnyCompoundBatch =
      AllowedBatches<CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;

  BatchTemplates() {
    BatchTemplateKey unevaluatedKey("Unevaluated", std::vector<size_t>(1, 0));
    m_templates[unevaluatedKey] = BatchTemplatePtr(new UnevaluatedBatchTemplate("Unevaluated"));
  }

  Batch::WritablePtr createBatch(Expression const& expression,
                                 bool decomposedDispatch = false) const override {
    return std::visit([this, &decomposedDispatch](
                          auto&& value) { return createBatch(value, decomposedDispatch); },
                      expression);
  }

  Batch::WritablePtr createBatch(Symbol const& symbol) const { return createBatch(symbol, false); }

  Batch::WritablePtr createBatch(Symbol const& symbol, bool /*decomposedDispatch*/) const {
    return Batch::WritablePtr(new SymbolBatch(*this, symbol));
  }

  Batch::WritablePtr createBatch(ComplexExpression const& expression,
                                 bool decomposedDispatch) const {
    auto const& symbol = expression.getHead();
    auto argsBegin = expression.getArguments().begin();
    auto argsEnd = expression.getArguments().end();
    size_t numArgs = std::distance(argsBegin, argsEnd);
    std::vector<size_t> argumentTypes(numArgs, 0);
    BatchTemplateKey key(symbol.getName(), argumentTypes);
    auto templateIt = m_templates.find(key);
    if(templateIt != m_templates.end()) {
      auto* batchTemplate = templateIt->second.get();
      return batchTemplate->createBatch(*this, decomposedDispatch);
    }

    if(numArgs > 1) {
      // if argument count doesn't match
      // try to find a function with less arguments and split
      // TODO: only if variadic is allowed on these functions
      auto closestTemplateIt = m_templates.lower_bound(key);
      if(closestTemplateIt != m_templates.end() && closestTemplateIt != m_templates.begin()) {
        --closestTemplateIt;
        auto* batchTemplate = closestTemplateIt->second.get();
        if(closestTemplateIt->first.getKey() == symbol.getName()) {
          if(closestTemplateIt->first.getArgumentCount() == 2) {
            // create a compound expression batch
            // (it will be handled at insert)
            return batchTemplate->createBatch(*this, decomposedDispatch);
          }
        }
      }
    }

    // symbol not found, return a generic batch with arguments at least
    return Batch::WritablePtr(new CompoundBatch(*this, symbol, decomposedDispatch));
  }

  template <typename T> Batch::WritablePtr createBatch(T const& value) const {
    return createBatch<T>(value, false);
  }

  template <typename T>
  Batch::WritablePtr createBatch(T const& value, bool /*decomposedDispatch*/) const {
    return Batch::WritablePtr(new RLEBatch<T>(value));
  }

  Batch::ReadablePtr extractFromBatch(Batch const& srcBatch, size_t index) const override {
    Batch::ReadablePtr outputBatchPtr;
    BatchHelper::visit(
        [this, &index, &outputBatchPtr](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          using ValueType = typename BatchType::ValueType;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            outputBatchPtr = batch.extract(index);
          } else {
            auto const& value = *(batch.begin() + index);
            auto newBatchPtr = createBatch((ValueType)value, false);
            newBatchPtr->insert(value);
            outputBatchPtr = std::move(newBatchPtr);
          }
        },
        srcBatch);
    return outputBatchPtr;
  }

  Batch::ReadablePtr recomposeBatch(Batch const& srcBatch, size_t index,
                                    bool decomposedDispatch) const override {
    // make a copy of it to receive the single element
    // by recomposing a new row from every batch
    Batch::ReadablePtr recomposedPtr;
    BatchHelper::visit(
        [this, &index, &recomposedPtr, &decomposedDispatch](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            if(batch.size() == 1) {
              auto recomposedCompoundPtr = batch.cloneAsCompoundBatch();
              recomposedCompoundPtr->setDecomposed(decomposedDispatch);
              recomposedPtr = std::move(recomposedCompoundPtr);
              return;
            }
            auto recomposedCompoundPtr = batch.cloneAsCompoundBatch(true);
            auto& recomposed = *recomposedCompoundPtr;
            recomposed.setDecomposed(decomposedDispatch);
            size_t newIndex = 0;
            batch.visitBatches([this, &recomposed, &index, &newIndex](auto const& columnKey,
                                                                      auto const& columnPtr) {
              auto valuePtr = extractFromBatch(*columnPtr, index);
              recomposed.insert(columnKey.first, newIndex++, std::move(valuePtr));
            });
            recomposedPtr = std::move(recomposedCompoundPtr);
          } else {
            // otherwise that is just an extraction
            recomposedPtr = extractFromBatch(batch, index);
          }
        },
        srcBatch);
    return recomposedPtr;
  }

  void reduceCompoundBatch(Batch& destBatch, Batch const& srcBatch, size_t index) const override {
    boss::engines::bulk::BatchHelper<CompoundBatch>::visit(
        [this, &destBatch, &index](auto const& srcCompoundBatch) {
          boss::engines::bulk::BatchHelper<CompoundBatch>::visit(
              [this, &srcCompoundBatch, &index](auto& destCompoundBatch) {
                size_t newIndex = 0;
                srcCompoundBatch.visitBatches([this, &destCompoundBatch, &index, &newIndex](
                                                  auto const& batchKey, auto const& batchPtr) {
                  auto reducedPtr = reduceBatch(batchPtr, index);
                  destCompoundBatch.insert(batchKey.first, newIndex++, std::move(reducedPtr));
                });
              },
              destBatch);
        },
        srcBatch);
  }

  Batch::ReadablePtr reduceBatch(Batch::ReadablePtr batchPtr, size_t index) const override {
    Batch::ReadablePtr reducedPtr;
    BatchHelper::visit(
        [this, &index, &batchPtr, &reducedPtr](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            reducedPtr = batch.reduce(index);
          } else {
            reducedPtr = std::move(batchPtr);
          }
        },
        *batchPtr);
    return reducedPtr;
  }

  Batch::ReadablePtr convertToNonRLE(Batch::ReadablePtr batchPtr) const override {
    auto const& batch = *batchPtr;
    Batch::WritablePtr newBatchPtr;
    if(batch.isRLE()) {
      BatchHelper::visit(
          [this, &newBatchPtr](auto const& batch) {
            using BatchType = std::decay_t<decltype(batch)>;
            using ValueType = typename BatchType::ValueType;
            if constexpr(!std::is_same_v<ValueType, Symbol> &&
                         !std::is_same_v<ValueType, ComplexExpression>) {
              newBatchPtr =
                  Batch::WritablePtr(new ValueBatch<ValueType>(batch.size(), *batch.begin()));
            }
          },
          batch);
    }

    if(newBatchPtr) {
      return std::move(newBatchPtr);
    }
    return std::move(batchPtr);
  }

  Batch::ReadablePtr convertToDecomposed(Batch::ReadablePtr batchPtr) const override {
    auto const& batch = *batchPtr;
    Batch::WritablePtr newBatchPtr;
    CompoundBatchHelper::visit(
        [this, &batchPtr, &newBatchPtr](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          if(!batch.isDecomposed()) {
            WritableBatchPtr<BatchType> writableCompoundBatchPtr(std::move(batchPtr));
            writableCompoundBatchPtr->setDecomposed(true);
            newBatchPtr = std::move(writableCompoundBatchPtr);
          }
        },
        batch);

    if(newBatchPtr) {
      return std::move(newBatchPtr);
    }
    return std::move(batchPtr);
  }

  Expression revertToExpression(Batch::ReadablePtr&& batchPtr) const override {
    if(batchPtr->typeId() == UniqueId::forType<TableView>()) {
      // save the query result into a temporary symbol
      // this is a workaround to avoid unevaluated call to return a whole table
      // TODO: find a way to garbage-collect them
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
    Symbol const* rootHead = nullptr;
    ExpressionArguments arguments;
    arguments.reserve(batch.size());
    BatchHelper::visit(
        [this, &arguments, &rootHead, batchPtr{std::move(batchPtr)}](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            rootHead = &batch.getHead();
            size_t batchSize = batch.size();
            for(size_t index = 0; index < batchSize; ++index) {
              auto extractedPtr = batch.extract(index);
              arguments.emplace_back(revertToExpression(std::move(extractedPtr)));
            }
          } else {
            for(auto const& value : batch) {
              arguments.emplace_back(value);
            }
          }
        },
        batch);

    if(arguments.size() == 1 && rootHead == nullptr) {
      return arguments[0];
    }

    Symbol const& head = rootHead != nullptr ? *rootHead : Symbol("List");
    return ComplexExpression(head, arguments);
  }

  Expression toKey(Batch const& batch) const override {
    Expression key;
    BatchHelper::visit(
        [this, &key](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          using ValueType = typename BatchType::ValueType;
          if constexpr(std::is_same_v<TableView, BatchType>) {
            key = batch.getHead().getName();
          } else if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            size_t batchSize = batch.size();
            ExpressionArguments arguments;
            arguments.reserve(batchSize);
            for(auto const& batchPtr : batch) {
              arguments.emplace_back(toKey(*batchPtr));
            }
            key = ComplexExpression(batch.getHead(), std::move(arguments));
          } else if constexpr(std::is_base_of_v<SymbolBatch, BatchType>) {
            key = *batch.begin();
          } else {
            key = ValueType();
          }
        },
        batch);

    return key;
  }

  friend class AllowedTypes;
  template <typename... Types> class AllowedTypes {
  public:
    explicit AllowedTypes(BatchTemplates& batchTemplates) : m_batchTemplates(batchTemplates) {}

    template <size_t N, typename Func>
    void registerFunction(std::string const& symbol, Func&& func) {
      std::vector<size_t> argumentTypes(N, 0);
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
      std::conditional_t<
          std::is_same_v<Types, Symbol>, AllowedTypes<AllowedBatches<SymbolBatch>>,
          std::conditional_t<
              std::is_same_v<Types, ComplexExpression>, AllowedTypes<AllowedBatches<CompoundBatch>>,
              AllowedTypes<AllowedBatches<ValueBatch<Types>, RLEBatch<Types>>>>>...>::type;

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

    virtual Batch::WritablePtr createBatch(BatchTemplates const&,
                                           bool decomposedDispatch) const = 0;
  };

  class UnevaluatedBatchTemplate : public BatchTemplateBase {
  public:
    explicit UnevaluatedBatchTemplate(std::string const& symbolName) : m_symbolName(symbolName) {}

    Batch::WritablePtr createBatch(BatchTemplates const& templates,
                                   bool decomposedDispatch) const override {
      return Batch::WritablePtr(
          new UnevaluatedBatch(templates, Symbol(m_symbolName), decomposedDispatch));
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

    Batch::WritablePtr createBatch(BatchTemplates const& templates,
                                   bool decomposedDispatch) const override {
      return Batch::WritablePtr(new BatchType(templates, m_evaluator, decomposedDispatch));
    }

  private:
    EvaluatorType m_evaluator;
  };

  using BatchTemplatePtr = std::unique_ptr<BatchTemplateBase>;
  std::map<BatchTemplateKey, BatchTemplatePtr> m_templates;
};

} // namespace boss::engines::bulk
