#pragma once

#include "BatchFactory.hpp"

#include "Batch/Batch.hpp"
#include "Batch/ExpressionBatch.hpp"
#include "Batch/RLEBatch.hpp"
#include "Batch/SymbolBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include "../../Expression.hpp"

#include <map>
#include <memory>

namespace boss::engines::bulk {

/********************* class Dispatcher ***********************/

/* insert any expression into its specific Batch type         */
/* deduced from the expression head and argument types        */
/**************************************************************/

class Dispatcher {
public:
  using BatchPtr = std::unique_ptr<Batch>;

  Dispatcher(BatchFactory const& factory) : m_factory(factory), m_size(0) {}

  Dispatcher(Dispatcher const& other, bool clear = false)
      : m_factory(other.m_factory), m_size(clear ? 0 : other.m_size) {
    for(auto& [key, otherBatchPtr] : other.m_batches) {
      m_batches[key] = std::move(otherBatchPtr.get()->clone(clear));
    }
  }

  BatchFactory const& getBatchFactory() const { return m_factory; }

  size_t size() const { return m_size; }

  void clear(bool keepEmptyBatches = false) {
    if(keepEmptyBatches) {
      for(auto& [key, batchPtr] : m_batches) {
        batchPtr.get()->clear();
      }
    } else {
      m_batches.clear();
    }
    m_size = 0;
  }

  void insert(Expression const& key, BatchPtr batch) {
    m_size += batch.get()->size();
    m_batches[key] = std::move(batch);
  }

  void insert(Expression const& argument) {
    auto& batchPtr = m_batches[argument];
    if(!batchPtr) {
      batchPtr = std::visit(
          [this](auto&& value) {
            using type = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<type, ComplexExpression>) {
              return m_factory.createBatch(value);
            } else if constexpr(std::is_same_v<type, Symbol>) {
              return BatchPtr(new SymbolBatch(value));
            } else {
              return BatchPtr(new RLEBatch<type>(value));
            }
          },
          argument);
    }

    if(!std::holds_alternative<Symbol>(argument) &&
       !std::holds_alternative<ComplexExpression>(argument)) {
      auto& batch = *batchPtr.get();
      if(batch.isRLE() && !batch.canContain(argument)) {
        // there are more than one single value
        // make it a normal value batch
        std::visit(
            [&batchPtr, &batch](auto&& value) {
              using type = std::decay_t<decltype(value)>;
              if constexpr(!std::is_same_v<type, Symbol> &&
                           !std::is_same_v<type, ComplexExpression>) {
                auto& RLEbatch = *static_cast<RLEBatch<type> const*>(&batch);
                batchPtr = BatchPtr(new ValueBatch<type>(RLEbatch.size(), *RLEbatch.begin()));
              }
            },
            argument);
      }
    }

    batchPtr.get()->insert(argument);
    ++m_size;
  }

  std::vector<BatchPtr> evaluate() const {
    std::vector<BatchPtr> output;
    output.reserve(m_batches.size());
    for(auto& [argument, batchPtr] : m_batches) {
      output.emplace_back(std::move(batchPtr.get()->evaluate()));
    }
    return output;
  }

  template <typename Func> void visitBatches(Func&& visitor) const {
    for(auto& [argument, batchPtr] : m_batches) {
      visitor(argument, *batchPtr.get());
    }
  }

private:
  struct compareExpressionType {
    bool operator()(Expression const& lhs, Expression const& rhs) const {
      return compare(lhs, rhs) < 0;
    }

  private:
    // TODO: save minimum information into a key
    // to avoid saving a full expression for each batch
    int compare(Expression const& lhs, Expression const& rhs) const {
      if(lhs.index() != rhs.index()) {
        return lhs.index() < rhs.index() ? -1 : 1;
      } else if(auto const* lhsSymbol = std::get_if<Symbol>(&lhs)) {
        auto& rhsSymbol = std::get<Symbol>(rhs);
        return lhsSymbol->getName() < rhsSymbol.getName() ? -1 : 1;
      } else if(auto const* lhsExpr = std::get_if<ComplexExpression>(&lhs)) {
        auto& rhsExpr = std::get<ComplexExpression>(rhs);
        if(lhsExpr->getHead().getName() != rhsExpr.getHead().getName()) {
          return lhsExpr->getHead().getName() < rhsExpr.getHead().getName() ? -1 : 1;
        } else {
          auto lhsArgsIt = lhsExpr->getArguments().begin();
          auto rhsArgsIt = rhsExpr.getArguments().begin();
          auto lhsArgsItEnd = lhsExpr->getArguments().end();
          auto rhsArgsItEnd = rhsExpr.getArguments().end();
          size_t lhsNumArgs = std::distance(lhsArgsIt, lhsArgsItEnd);
          size_t rhsNumArgs = std::distance(rhsArgsIt, rhsArgsItEnd);
          if(lhsNumArgs != rhsNumArgs) {
            return lhsNumArgs < rhsNumArgs ? -1 : 1;
          } else {
            while(lhsArgsIt != lhsArgsItEnd /*&& rhsArgsIt != rhsArgsItEnd*/) {
              int argCompare = compare(*lhsArgsIt, *rhsArgsIt);
              if(argCompare != 0) {
                return argCompare;
              }
              ++lhsArgsIt;
              ++rhsArgsIt;
            }

            // identical arguments
            return 0;
          }
        }
      } else {
        // "normal" values (of identical type) are all dispatched to the same batch
        return 0;
      }
    }
  };

  std::map<Expression, BatchPtr, compareExpressionType> m_batches;

  BatchFactory const& m_factory;

  size_t m_size;
};

} // namespace boss::engines::bulk
