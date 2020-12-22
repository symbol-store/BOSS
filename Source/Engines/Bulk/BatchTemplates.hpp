#pragma once

#include "BatchFactory.hpp"
#include "Evaluator.hpp"

#include "Batch/Batch.hpp"
#include "Batch/ExpressionBatch.hpp"
#include "Batch/RLEBatch.hpp"
#include "Batch/SymbolBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include "../../Expression.hpp"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace boss::engines::bulk {

class BatchTemplates : public BatchFactory {
public:
  BatchTemplates() = default;
  BatchTemplates(BatchTemplates const&) = delete;

  friend class AllowedTypes;
  template <typename... Types> class AllowedTypes {
  public:
    AllowedTypes(BatchTemplates& batchTemplates) : m_batchTemplates(batchTemplates) {}

    template <size_t N, typename Func>
    void registerFunction(std::string const& symbol, Func&& func) {
      registerFunction<Func, N>(symbol, std::move(func));
    }

    template <typename Func, size_t N = lambda_details<Func>::argument_count>
    void registerFunction(std::string const& symbol, Func&& func) {
      using BatchTemplateType = BatchTemplate<Func, N, Types...>;
      using BaseBatchType = typename BatchTemplateType::BatchType;

      // TODO: fill argumentTypes with type ids if needed to handle overloading
      std::vector<size_t> argumentTypes(N, 0);
      BatchTemplateKey key(symbol, argumentTypes);
      auto* expressionBatch = new BaseBatchType(std::move(func));
      auto* templateBatch = new BatchTemplateType(expressionBatch);
      m_batchTemplates.m_templates[key] = BatchTemplatePtr(templateBatch);
    }

  private:
    BatchTemplates& m_batchTemplates;
  };

  template <typename... Types> AllowedTypes<Types...>& allowedTypes() {
    static AllowedTypes<Types...> types(*this);
    return types;
  }

  Batch* createBatch(Expression const& expression) const {
    std::string const& symbol = expression.getHead();
    auto argsBegin = expression.getArguments().begin();
    auto argsEnd = expression.getArguments().end();
    size_t numArgs = std::distance(argsBegin, argsEnd);
    std::vector<size_t> argumentTypes(numArgs, 0);
    BatchTemplateKey key(symbol, argumentTypes);
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
          if(closestTemplateIt->first.getKey() == symbol &&
             closestTemplateIt->first.getArgumentCount() == 2) {
            // create a compound expression batch
            auto* batchTemplate = closestTemplateIt->second.get();
            auto it = std::next(argsBegin, 2);
            Expression::ArgumentList newArgumentList{argsBegin, it};
            for(; it != argsEnd; ++it) {
              Expression compoundExpr{symbol, newArgumentList};
              newArgumentList = Expression::ArgumentList{compoundExpr, *it};
            }
            return batchTemplate->createBatch(*this, newArgumentList);
          }
        }
      }

      return nullptr;
    }
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
    virtual Batch* createBatch(BatchTemplates const&,
                               Expression::ArgumentList const& argumentList) const {
      return nullptr;
    }
  };

  template <typename Func, int N, typename... AllowedTypes>
  class BatchTemplate : public BatchTemplateBase {
  public:
    using EvaluatorType = typename ForTypes<AllowedTypes...>::template Evaluator<Func>;
    using BatchType = ExpressionBatchBase<EvaluatorType, Func, N>;

    BatchTemplate(BatchType* batch) : m_batch(batch) {}

    Batch* createBatch(BatchTemplates const& templates,
                       Expression::ArgumentList const& argumentList) const override {
      if(!m_batch) {
        return nullptr;
      }

      return templates.createBatchHelper<N>(*m_batch.get(), argumentList.begin(),
                                            argumentList.end());
    }

  private:
    std::unique_ptr<BatchType> m_batch;
  };

  using BatchTemplatePtr = std::unique_ptr<BatchTemplateBase>;
  std::map<BatchTemplateKey, BatchTemplatePtr> m_templates;

  template <size_t N, typename BatchType, typename Variants, typename End, typename Tuple = bool>
  Batch* createBatchHelper(BatchType const& batch, Variants variants, End end,
                           Tuple&& tuple = false) const {
    if constexpr(N == 0) {
      if constexpr(std::is_same_v<Tuple, bool>) {
        return nullptr;
      } else {
        return clone(batch, std::move(tuple));
      }
    } else {
      return std::visit(
          [&, this](auto&& value) -> Batch* {
            using type = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<type, Expression>) {
              if constexpr(std::is_same_v<Tuple, bool>) {
                auto newTuple = std::make_tuple(std::unique_ptr<Batch>(createBatch(value)));
                return createBatchHelper<N - 1>(batch, ++variants, end, newTuple);
              } else {
                return std::apply(
                    [&, this](auto&&... arg) {
                      return createBatchHelper<N - 1>(
                          batch, ++variants, end,
                          std::make_tuple((std::move(arg))...,
                                          std::unique_ptr<Batch>(createBatch(value))));
                    },
                    tuple);
              }
            } else if constexpr(std::is_same_v<type, Expression::Symbol>) {
              if constexpr(std::is_same_v<Tuple, bool>) {
                auto newTuple =
                    std::make_tuple(std::unique_ptr<SymbolBatch>(new SymbolBatch(value)));
                return createBatchHelper<N - 1>(batch, ++variants, end, newTuple);
              } else {
                return std::apply(
                    [&, this](auto&&... arg) {
                      return createBatchHelper<N - 1>(
                          batch, ++variants, end,
                          std::make_tuple((std::move(arg))...,
                                          std::unique_ptr<SymbolBatch>(new SymbolBatch(value))));
                    },
                    tuple);
              }
            } else {
              if constexpr(std::is_same_v<Tuple, bool>) {
                auto newTuple =
                    std::make_tuple(std::unique_ptr<ValueBatch<type>>(new ValueBatch<type>()));
                return createBatchHelper<N - 1>(batch, ++variants, end, newTuple);
              } else {
                return std::apply(
                    [&, this](auto&&... arg) {
                      return createBatchHelper<N - 1>(
                          batch, ++variants, end,
                          std::make_tuple((std::move(arg))..., std::unique_ptr<ValueBatch<type>>(
                                                                   new ValueBatch<type>())));
                    },
                    tuple);
              }
            }
          },
          *variants);
    }
  }
};

} // namespace boss::engines::bulk
