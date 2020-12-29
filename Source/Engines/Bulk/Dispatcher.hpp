#pragma once

#include "BatchTemplates.hpp"

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

  Dispatcher(BatchTemplates const& templates) : m_templates(templates) {}

  void insert(Expression const& argument) {
    auto& batchPtr = m_batches[argument];
    if(!batchPtr) {
      batchPtr = std::visit(
          [this](auto&& value) {
            using type = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<type, ComplexExpression>) {
              return BatchPtr(m_templates.createBatch(value));
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
              batchPtr = BatchPtr(new ValueBatch<type>(batch.size(), value));
            },
            argument);
      }
    }

    // TODO: when batchPtr is null,
    // should still return the expression as much as it could be evaluated
    if(batchPtr) {
      batchPtr.get()->insert(argument);
    }
  }

  std::vector<BatchPtr> evaluate() {
    std::vector<BatchPtr> output;
    output.reserve(m_batches.size());
    for(auto &[argument, batchPtr] : m_batches) {
      if(batchPtr) {
        output.emplace_back(batchPtr.get()->evaluate(m_templates));
      }
    }
    return output;
  }

  void clear() {
    m_batches.clear();
  }

private:
  struct compareExpressionType {
    bool operator()(Expression const& lhs,
                    Expression const& rhs) const {
      return compare(lhs, rhs) < 0;
    }

  private:
    int compare(Expression const& lhs, Expression const& rhs) const {
      if(lhs.index() != rhs.index()) {
        return lhs.index() < rhs.index() ? -1 : 1;
      } else if(auto const* lhsSymbol = std::get_if<Symbol>(&lhs)) {
        auto& rhsSymbol = std::get<Symbol>(rhs);
        return lhsSymbol->getName() < rhsSymbol.getName() ? -1 : 1;
      } else if(auto const* lhsExpr = std::get_if<ComplexExpression>(&lhs)) {
        auto& rhsExpr = std::get<ComplexExpression>(rhs);
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
      } else {
        // "normal" values (of identical type) are all dispatched to the same batch
        return 0;
      }
    }
  };

  std::map<Expression, BatchPtr, compareExpressionType> m_batches;

  BatchTemplates const& m_templates;
};

} // namespace boss::engines::bulk
