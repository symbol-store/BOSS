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
  BatchPtr createBatch(Expression const& expression, bool allowDecomposedDispatch) const override {
    return std::visit([this, &allowDecomposedDispatch](
                          auto&& value) { return createBatch(value, allowDecomposedDispatch); },
                      expression);
  }

  BatchPtr createBatch(Symbol const& symbol, bool /*allowDecomposedDispatch*/) const {
    return BatchPtr(new SymbolBatch(symbol));
  }

  BatchPtr createBatch(ComplexExpression const& expression, bool allowDecomposedDispatch) const {
    auto const& symbol = expression.getHead();
    auto argsBegin = expression.getArguments().begin();
    auto argsEnd = expression.getArguments().end();
    size_t numArgs = std::distance(argsBegin, argsEnd);
    std::vector<size_t> argumentTypes(numArgs, 0);
    BatchTemplateKey key(symbol.getName(), argumentTypes);
    auto templateIt = m_templates.find(key);
    if(templateIt != m_templates.end()) {
      auto* batchTemplate = templateIt->second.get();
      return batchTemplate->createBatch(*this);
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
            return batchTemplate->createBatch(*this);
          }
        }
      }
    }

    // symbol not found, return a generic batch with arguments at least
    return BatchPtr(new CompoundBatch(*this, allowDecomposedDispatch, symbol));
  }

  BatchPtr extractFromBatch(Batch const& batch, size_t index) const override {
    BatchPtr batchPtr;
    BatchHelper::visit(
        [this, &index, &batchPtr](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          using ValueType = typename BatchType::ValueType;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            batchPtr = batch.extract(index);
          } else {
            auto const& value = *(batch.begin() + index);
            batchPtr = createBatch((ValueType)value, false);
            batchPtr->insert(value);
          }
        },
        batch);
    return batchPtr;
  }

  BatchPtr recomposeBatch(Batch const& batch, size_t index) const override {
    // make a copy of it to receive the single element
    // by recomposing a new row from every batch
    BatchPtr recomposedPtr;
    BatchHelper::visit(
        [this, &index, &recomposedPtr](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            auto recomposedCompoundPtr = batch.cloneAsCompoundBatch(true);
            auto& recomposed = *recomposedCompoundPtr;
            recomposed.setDecomposedDispatch(true);
            size_t newIndex = 0;
            batch.visitBatches(
                [this, &recomposed, &index, &newIndex](auto const& columnKey, auto const& column) {
                  auto valuePtr = extractFromBatch(column, index);
                  recomposed.insert(columnKey.first, newIndex++, std::move(valuePtr));
                });
            recomposedPtr = std::move(recomposedCompoundPtr);
          } else {
            // otherwise that is just an extraction
            recomposedPtr = extractFromBatch(batch, index);
          }
        },
        batch);
    return recomposedPtr;
  }

  void reduceCompoundBatch(Batch& destBatch, Batch const& srcBatch, size_t index) const override {
    boss::engines::bulk::BatchHelper<CompoundBatch>::visit(
        [this, &destBatch, &index](auto const& srcCompoundBatch) {
          boss::engines::bulk::BatchHelper<CompoundBatch>::visit(
              [this, &srcCompoundBatch, &index](auto& destCompoundBatch) {
                size_t newIndex = 0;
                srcCompoundBatch.visitBatches([this, &destCompoundBatch, &index,
                                               &newIndex](auto const& batchKey, auto const& batch) {
                  auto reducedPtr = reduceBatch(batch, index);
                  destCompoundBatch.insert(batchKey.first, newIndex++, std::move(reducedPtr));
                });
              },
              destBatch);
        },
        srcBatch);
  }

  BatchPtr reduceBatch(Batch const& batch, size_t index) const override {
    BatchPtr reducedPtr;
    BatchHelper::visit(
        [this, &index, &reducedPtr](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            reducedPtr = batch.reduce(index);
          } else {
            reducedPtr = batch.clone();
          }
        },
        batch);
    return reducedPtr;
  }

  BatchPtr convertToNonRLE(Batch& batch) const override {
    BatchPtr newBatchPtr;
    if(batch.isRLE()) {
      BatchHelper::visit(
          [this, &newBatchPtr](auto const& batch) {
            using BatchType = std::decay_t<decltype(batch)>;
            using ValueType = typename BatchType::ValueType;
            if constexpr(!std::is_same_v<ValueType, Symbol> &&
                         !std::is_same_v<ValueType, ComplexExpression>) {
              newBatchPtr = BatchPtr(new ValueBatch<ValueType>(batch.size(), *batch.begin()));
            }
          },
          batch);
    }

    if(newBatchPtr) {
      return newBatchPtr;
    }

    return batch.clone();
  }

  Expression revertToExpression(Batch const& batch) const override {
    Symbol const* rootHead = nullptr;
    ExpressionArguments arguments;
    arguments.reserve(batch.size());
    BatchHelper::visit(
        [this, &arguments, &rootHead](auto const& batch) {
          using BatchType = std::decay_t<decltype(batch)>;
          if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
            rootHead = &batch.getHead();
            size_t batchSize = batch.size();
            for(size_t index = 0; index < batchSize; ++index) {
              auto batchPtr = batch.extract(index);
              arguments.emplace_back(revertToExpression(*batchPtr));
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

  friend class AllowedTypes;
  template <bool FixedTypes, typename... Types> class AllowedTypes {
  public:
    using BatchHelper = BatchHelper<Types...>;

    explicit AllowedTypes(BatchTemplates& batchTemplates) : m_batchTemplates(batchTemplates) {}

    template <size_t N, typename Func>
    void registerFunction(std::string const& symbol, Func&& func) {
      std::vector<size_t> argumentTypes(N, 0);
      /*if constexpr(FixedTypes) {
        // support overloading only when specify every argument type
        if constexpr(IsBatchType) {
          argumentTypes = std::vector<size_t>{((size_t)UniqueId::forType<typename
      Types::ValueType>())...}; } else { argumentTypes =
      std::vector<size_t>{((size_t)UniqueId::forType<Types>())...};
        }
      }*/
      BatchTemplateKey key(symbol, argumentTypes);
      auto* templateBatch = new ExpressionBatchTemplate<Func, N, FixedTypes, Types...>(
          symbol, std::forward<Func>(func));
      m_batchTemplates.m_templates[key] = BatchTemplatePtr(templateBatch);
    }

  private:
    BatchTemplates& m_batchTemplates;
  };

  using AnyTypes =
      AllowedTypes<false, ValueBatch<SupportedTypes>..., RLEBatch<SupportedTypes>..., SymbolBatch,
                   CompoundBatch, FunctionBatch, AnyExpressionBatch, TableView>;
  using BatchHelper = typename AnyTypes::BatchHelper;

  auto any() { return AnyTypes(*this); }
  template <typename... Types> auto allowedTypes() {
    return FromElementTypeToAllowedBatchTypes<false, Types...>(*this);
  }
  template <typename... Types> auto argTypes() {
    return FromElementTypeToAllowedBatchTypes<true, Types...>(*this);
  }
  template <typename... Types> auto allowedBatchTypes() {
    return AllowedTypes<false, Types...>(*this);
  }
  template <typename... Types> auto argBatchTypes() { return AllowedTypes<true, Types...>(*this); }

  template <typename T>
  BatchPtr createBatch(T const& value, bool /*allowDecomposedDispatch*/) const {
    return BatchPtr(new RLEBatch<T>(value));
  }

private:
  template <typename, typename> struct MergeTwoAllowedTypes;
  template <bool UsingFixedTypes, typename... Args0, typename... Args1>
  struct MergeTwoAllowedTypes<AllowedTypes<UsingFixedTypes, Args0...>,
                              AllowedTypes<UsingFixedTypes, Args1...>> {
    using type = AllowedTypes<UsingFixedTypes, Args0..., Args1...>;
  };

  template <typename...> struct MergeAllowedTypes;
  template <typename FirstAllowedType> struct MergeAllowedTypes<FirstAllowedType> {
    using type = FirstAllowedType;
  };
  template <typename FirstAllowedType, typename... OtherAllowedTypes>
  struct MergeAllowedTypes<FirstAllowedType, OtherAllowedTypes...> {
    using type =
        typename MergeTwoAllowedTypes<FirstAllowedType,
                                      typename MergeAllowedTypes<OtherAllowedTypes...>::type>::type;
  };

  template <bool UsingFixedTypes, typename... Types>
  using FromElementTypeToAllowedBatchTypes = typename MergeAllowedTypes<std::conditional_t<
      std::is_same_v<Types, Symbol>, AllowedTypes<UsingFixedTypes, SymbolBatch>,
      std::conditional_t<
          std::is_same_v<Types, ComplexExpression>, AllowedTypes<UsingFixedTypes, CompoundBatch>,
          AllowedTypes<UsingFixedTypes, ValueBatch<Types>, RLEBatch<Types>>>>...>::type;

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

    virtual BatchPtr createBatch(BatchTemplates const&) const = 0;
  };

  template <typename Func, int N, bool FixedTypes, typename... AllowedTypes>
  class ExpressionBatchTemplate : public BatchTemplateBase {
  public:
    using EvaluatorType = typename ForTypes<AllowedTypes...>::template Evaluator<Func>;
    using BatchType = ExpressionBatch<EvaluatorType, Func, N, FixedTypes>;

    ExpressionBatchTemplate(std::string const& symbol, Func&& func) : m_evaluator(symbol, func) {}

    BatchPtr createBatch(BatchTemplates const& templates) const override {
      return BatchPtr(new BatchType(templates, m_evaluator));
    }

  private:
    EvaluatorType m_evaluator;
  };

  using BatchTemplatePtr = std::unique_ptr<BatchTemplateBase>;
  std::map<BatchTemplateKey, BatchTemplatePtr> m_templates;
};

} // namespace boss::engines::bulk
