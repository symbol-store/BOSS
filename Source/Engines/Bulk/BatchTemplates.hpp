#pragma once

#include "BatchFactory.hpp"
#include "Evaluator.hpp"

#include "Batch/Batch.hpp"
#include "Batch/CompoundBatch.hpp"
#include "Batch/ExpressionBatch.hpp"
#include "Batch/RLEBatch.hpp"
#include "Batch/SymbolBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include "../../Expression.hpp"
#include "../../Utilities.hpp"

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using boss::utilities::operator""_;

namespace boss::engines::bulk {

/******************* class BatchTemplates *********************/

/* keep a map of Evaluators for each symbol                   */
/* then createBatch can create the right ExpressionBatch      */
/* for any complex expression                                 */
/**************************************************************/

template <typename... SupportedTypes> class BatchTemplates : public BatchFactory {
public:
  using CompoundBatch = CompoundBatch<SupportedTypes...>;
  using AnyExpressionBatch = AnyExpressionBatch<SupportedTypes...>;
  BatchTemplates() {}

  BatchTemplates(BatchTemplates const&) = delete;

  friend class AllowedTypes;
  template <bool FixedTypes, typename... Types> class AllowedTypes {
  public:
    AllowedTypes(BatchTemplates& batchTemplates) : m_batchTemplates(batchTemplates) {}

    template <size_t N, typename Func>
    void registerFunction(std::string const& symbol, Func&& func) {
      // TODO: fill argumentTypes with type ids if needed to handle overloading
      std::vector<size_t> argumentTypes(N, 0);
      BatchTemplateKey key(symbol, argumentTypes);
      auto* templateBatch =
          new ExpressionBatchTemplate<Func, N, FixedTypes, Types...>(symbol, std::move(func));
      m_batchTemplates.m_templates[key] = BatchTemplatePtr(templateBatch);
    }

  private:
    BatchTemplates& m_batchTemplates;
  };

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

  BatchPtr createBatch(ComplexExpression const& expression) const {
    auto const& symbol = expression.getHead();
    auto argsBegin = expression.getArguments().begin();
    auto argsEnd = expression.getArguments().end();
    size_t numArgs = std::distance(argsBegin, argsEnd);
    std::vector<size_t> argumentTypes(numArgs, 0);
    BatchTemplateKey key(symbol.getName(), argumentTypes);
    auto templateIt = m_templates.find(key);
    if(templateIt != m_templates.end()) {
      auto* batchTemplate = templateIt->second.get();
      return batchTemplate->createBatch(*this, expression.getArguments());
    } else {
      if(numArgs > 1) {
        // if argument count doesn't match
        // try to find a function with less arguments and split
        // TODO: only if variadic is allowed on these functions
        auto closestTemplateIt = m_templates.lower_bound(key);
        if(closestTemplateIt != m_templates.end() && closestTemplateIt != m_templates.begin()) {
          --closestTemplateIt;
          auto* batchTemplate = closestTemplateIt->second.get();
          if(closestTemplateIt->first.getKey() == symbol.getName()) {
            if(numArgs <= batchTemplate->maxArgCount()) {
              // the template can handle the number of args
              return batchTemplate->createBatch(*this, expression.getArguments());
            } else if(closestTemplateIt->first.getArgumentCount() == 2) {
              // create a compound expression batch
              auto it = std::next(argsBegin, 2);
              ExpressionArguments newArgumentList{argsBegin, it};
              for(; it != argsEnd; ++it) {
                ComplexExpression compoundExpr{symbol, newArgumentList};
                newArgumentList = ExpressionArguments{compoundExpr, *it};
              }
              return batchTemplate->createBatch(*this, newArgumentList);
            }
          }
        }
      }

      // symbol not found, return a generic batch with arguments at least
      return BatchPtr(new CompoundBatch(symbol, createBatchArguments(expression.getArguments())));
    }
  }

  typename CompoundBatch::BatchList
  createBatchArguments(ExpressionArguments const& argumentList) const {
    auto argsBegin = argumentList.begin();
    auto argsEnd = argumentList.end();
    size_t sizeArgs = std::distance(argsBegin, argsEnd);

    typename CompoundBatch::BatchList batches;
    batches.reserve(sizeArgs);
    for(auto const& arg : argumentList) {
      batches.emplace_back(std::visit(
          [this](auto&& value) {
            using type = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<type, ComplexExpression>) {
              return createBatch(value);
            } else if constexpr(std::is_same_v<type, Symbol>) {
              return BatchPtr(new SymbolBatch(value));
            } else {
              // assume RLE first, converted to ValueBatch if needed later
              return BatchPtr(new RLEBatch<type>(type()));
            }
          },
          arg));
    }
    return batches;
  }

private:
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
      } else if(m_argumentTypes.size() != rhs.m_argumentTypes.size()) {
        return m_argumentTypes.size() < rhs.m_argumentTypes.size();
      } else {
        for(int i = 0; i < m_argumentTypes.size(); ++i) {
          if(m_argumentTypes[i] != rhs.m_argumentTypes[i]) {
            return m_argumentTypes[i] < rhs.m_argumentTypes[i];
          }
        }
        return false;
      }
    }

  private:
    std::string m_symbol;
    std::vector<size_t> m_argumentTypes;
  };

  class BatchTemplateBase {
  public:
    virtual ~BatchTemplateBase() = default;
    virtual BatchPtr createBatch(BatchTemplates const&,
                                 ExpressionArguments const& argumentList) const = 0;

    virtual size_t maxArgCount() const = 0;
  };

  template <typename Func, int N, bool FixedTypes, typename... AllowedTypes>
  class ExpressionBatchTemplate : public BatchTemplateBase {
  public:
    using EvaluatorType = typename ForTypes<AllowedTypes...>::template Evaluator<Func>;
    using BatchType = ExpressionBatch<EvaluatorType, Func, N, FixedTypes, SupportedTypes...>;

    ExpressionBatchTemplate(std::string const& symbol, Func&& func) : m_evaluator(symbol, func) {}

    BatchPtr createBatch(BatchTemplates const& templates,
                         ExpressionArguments const& argumentList) const override {
      return BatchPtr(new BatchType(m_evaluator, templates.createBatchArguments(argumentList)));
    }

    size_t maxArgCount() const override { return N; }

  private:
    EvaluatorType m_evaluator;
  };

  using BatchTemplatePtr = std::unique_ptr<BatchTemplateBase>;
  std::map<BatchTemplateKey, BatchTemplatePtr> m_templates;
};

} // namespace boss::engines::bulk
