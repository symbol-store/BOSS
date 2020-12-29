#pragma once

#include "BatchFactory.hpp"
#include "Evaluator.hpp"

#include "Batch/Batch.hpp"
#include "Batch/ExpressionBatch.hpp"
#include "Batch/RLEBatch.hpp"
#include "Batch/SymbolBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include "Utils/LambdaInfo.hpp"

#include "../../Expression.hpp"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace boss::engines::bulk {

/******************* class BatchTemplates *********************/

/* keep a map of Evaluators for each symbol                   */
/* then createBatch can create the right ExpressionBatch      */
/* for any complex expression                                 */
/**************************************************************/

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

    template <typename Func, size_t N = LambdaInfo<Func>::ArgCount>
    void registerFunction(std::string const& symbol, Func&& func) {
      // TODO: fill argumentTypes with type ids if needed to handle overloading
      std::vector<size_t> argumentTypes(N, 0);
      BatchTemplateKey key(symbol, argumentTypes);
      auto* templateBatch = new BatchTemplate<Func, N, Types...>(std::move(func));
      m_batchTemplates.m_templates[key] = BatchTemplatePtr(templateBatch);
    }

  private:
    BatchTemplates& m_batchTemplates;
  };

  template <typename... Types> AllowedTypes<Types...>& allowedTypes() {
    static AllowedTypes<Types...> types(*this);
    return types;
  }

  Batch* createBatch(ComplexExpression const& expression) const {
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
          if(closestTemplateIt->first.getKey() == symbol.getName() &&
             closestTemplateIt->first.getArgumentCount() == 2) {
            // create a compound expression batch
            auto* batchTemplate = closestTemplateIt->second.get();
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
                               ExpressionArguments const& argumentList) const {
      return nullptr;
    }
  };

  template <typename Func, int N, typename... AllowedTypes>
  class BatchTemplate : public BatchTemplateBase {
  public:
    using EvaluatorType = typename ForTypes<AllowedTypes...>::template Evaluator<Func>;
    using BatchType = ExpressionBatch<EvaluatorType, Func, N>;

    BatchTemplate(Func&& func) : m_evaluator(func) {}

    Batch* createBatch(BatchTemplates const& templates,
                       ExpressionArguments const& argumentList) const override {
      auto argIt = argumentList.begin();

      typename BatchType::ArgumentList batchArgs;
      for(size_t i = 0; i < N; ++i) {
        batchArgs[i] = std::visit(
            [&templates](auto&& value) {
              using type = std::decay_t<decltype(value)>;
              if constexpr(std::is_same_v<type, ComplexExpression>) {
                return std::unique_ptr<Batch>(templates.createBatch(value));
              } else if constexpr(std::is_same_v<type, Symbol>) {
                return std::unique_ptr<Batch>(new SymbolBatch(value));
              } else {
                // assume RLE first, converted to ValueBatch if needed later
                return std::unique_ptr<Batch>(new RLEBatch<type>());
              }
            },
            *argIt++);
      }

      return new BatchType(m_evaluator, std::move(batchArgs));
    }

  private:
    EvaluatorType m_evaluator;
  };

  using BatchTemplatePtr = std::unique_ptr<BatchTemplateBase>;
  std::map<BatchTemplateKey, BatchTemplatePtr> m_templates;
};

} // namespace boss::engines::bulk
