#pragma once

#include "BatchFactory.hpp"
#include "BatchHelper.hpp"

#include "Batch/Batch.hpp"
#include "Batch/RLEBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include "../../Expression.hpp"

#include <map>

namespace boss::engines::bulk {

/********************* class Dispatcher ***********************/

/* insert any expression into its specific Batch type         */
/* deduced from the expression head and argument types        */
/**************************************************************/

class Dispatcher {
public:
  Dispatcher(BatchFactory const& factory, bool decomposed)
      : m_factory(factory), m_decomposed(decomposed) {}

  Dispatcher(Dispatcher const& other, bool clear = false)
      : m_factory(other.m_factory), m_decomposed(other.m_decomposed) {
    if(!clear) {
      for(auto const& [key, otherBatchPtr] : other.m_batches) {
        insert(key.first, key.second, std::move(otherBatchPtr->clone(clear)));
      }
    }
  }

  ~Dispatcher() = default;
  Dispatcher(Dispatcher&& other) = default;
  Dispatcher& operator=(Dispatcher const& other) = delete;
  Dispatcher& operator=(Dispatcher&& other) = delete;

  void setDecomposed(bool value) { m_decomposed = value; }

  auto begin() const { return m_batches.begin(); }
  auto end() const { return m_batches.end(); }

  auto begin() { return m_batches.begin(); }
  auto end() { return m_batches.end(); }

  void clear() { m_batches.clear(); }

  size_t size() const {
    if(m_decomposed) {
      size_t totalSize = 0;
      visitBatches(
          [&totalSize](auto const& /*key*/, auto const& batch) { totalSize += batch.size(); });
      return totalSize;
    }
    return m_batches.empty() ? 0 : m_batches.begin()->second->size();
  }

  void resize(size_t size, Expression const& val) {
    if(m_decomposed) {
      // TODO: implements if needed
    } else {
      if(size < m_batches.size()) {
        BatchMap newBatches;
        for(auto& [key, batchPtr] : m_batches) {
          newBatches[key] = std::move(batchPtr);
        }
        m_batches.swap(newBatches);
      } else {
        size_t sizeDiff = size - m_batches.size();
        size_t indexOffset = m_batches.empty() ? 0 : 1 + m_batches.rbegin()->first.second;
        for(size_t argIndex = 0; argIndex < sizeDiff; ++argIndex) {
          insert(val, argIndex + indexOffset, BatchPtr());
        }
      }
    }
  }

  BatchPtr extract(Batch const& srcBatch, size_t index) const {
    return extract(srcBatch, index, m_batches.begin());
  }

  template <typename BatchIterator>
  BatchPtr extract(Batch const& srcBatch, size_t index, BatchIterator it) const {
    if(m_decomposed) {
      auto const& [key, batchPtr] = *it;
      // first find which batch contains the index
      // TODO: optimise the search...
      auto const& batch = *batchPtr;
      size_t batchSize = batch.size();
      if(index >= batchSize) {
        return extract(srcBatch, index - batchSize, ++it);
      }
      return m_factory.extractFromBatch(batch, index);
    }

    // need to recompose a new row from every column
    // Notes:
    // 1. we have to go through that even if a relation has only one row (size() == 1)
    //    so it creates the right decomposed batch and avoid an infinite loop...)
    // 2. we have to go through that even if a dispatched batch has only one column
    // (m_batches.size() == 1)
    //    otherwise we end up exporting a column as value directly instead of a list
    return m_factory.recomposeBatch(srcBatch, index);
  }

  BatchPtr reduce(Batch const& srcBatch, size_t index) const {
    if(m_decomposed) {
      auto destBatchPtr = srcBatch.clone(true);
      m_factory.reduceCompoundBatch(*destBatchPtr, srcBatch, index);
      return std::move(destBatchPtr);
    }
    return std::next(begin(), static_cast<ptrdiff_t>(index))->second->clone();
  }

  void insert(Expression const& argument, size_t argIndex, BatchPtr batchPtr) {
    DispatchKey key(argument, m_decomposed ? 0 : argIndex);
    auto& existingBatchPtr = m_batches[key];
    if(existingBatchPtr) {
      if(existingBatchPtr->isRLE()) {
        // need to recreate a non-RLE to be safe
        existingBatchPtr = m_factory.convertToNonRLE(*existingBatchPtr);
        // and so far the other batch too need to be same type
        batchPtr = m_factory.convertToNonRLE(*batchPtr);
      }
      existingBatchPtr->merge(std::move(batchPtr));
    } else {
      existingBatchPtr = std::move(batchPtr);
    }
  }

  void insert(Expression const& argument, size_t argIndex = 0) {
    DispatchKey key(argument, m_decomposed ? 0 : argIndex);
    auto& batchPtr = m_batches[key];
    if(!batchPtr) {
      bool allowDecomposedDispatch = !m_decomposed;
      batchPtr = m_factory.createBatch(argument, allowDecomposedDispatch);
    }

    if(!std::holds_alternative<Symbol>(argument) &&
       !std::holds_alternative<ComplexExpression>(argument)) {
      auto& batch = *batchPtr;
      if(batch.isRLE() && !batch.canContain(argument)) {
        // there are more than one single value
        // make it a normal value batch
        batchPtr = m_factory.convertToNonRLE(batch);
      }
    }

    batchPtr->insert(argument);
  }

  void merge(Dispatcher&& other) {
    for(auto& [argument, batchPtr] : other.m_batches) {
      insert(argument.first, argument.second, std::move(batchPtr));
    }
  }

  template <typename Func> void visitBatches(Func&& visitor) const {
    for(auto const& [argument, batchPtr] : m_batches) {
      visitor(argument, *batchPtr);
    }
  }

  template <typename BatchHelper, typename Func> void visitBatches(Func&& visitor) const {
    for(auto const& keyValue : m_batches) {
      auto const& argument = keyValue.first;
      auto const& batchPtr = keyValue.second;
      BatchHelper::visit(
          [&argument, &visitor](auto const& batch) {
            if(batch.size() == 0) {
              return;
            }
            visitor(argument, batch);
          },
          *batchPtr);
    }
  }

private:
  using DispatchKey = std::pair<Expression, size_t>;

  struct compareDispatchKey {
    bool operator()(DispatchKey const& lhs, DispatchKey const& rhs) const {
      auto const& [lhs_expr, lhs_argIndex] = lhs;
      auto const& [rhs_expr, rhs_argIndex] = rhs;
      if(lhs_argIndex != rhs_argIndex) {
        return lhs_argIndex < rhs_argIndex;
      }
      return compare(lhs_expr, rhs_expr) < 0;
    }

  private:
    // TODO: save minimum information into a key
    // to avoid saving a full expression for each batch
    int compare(Expression const& lhs, Expression const& rhs) const {
      if(lhs.index() != rhs.index()) {
        return lhs.index() < rhs.index() ? -1 : 1;
      }

      if(auto const* lhsSymbol = std::get_if<Symbol>(&lhs)) {
        auto const& rhsSymbol = std::get<Symbol>(rhs);
        return lhsSymbol->getName() < rhsSymbol.getName() ? -1 : 1;
      }

      if(auto const* lhsExpr = std::get_if<ComplexExpression>(&lhs)) {
        auto const& rhsExpr = std::get<ComplexExpression>(rhs);
        if(lhsExpr->getHead().getName() != rhsExpr.getHead().getName()) {
          return lhsExpr->getHead().getName() < rhsExpr.getHead().getName() ? -1 : 1;
        }

        auto lhsArgsIt = lhsExpr->getArguments().begin();
        auto rhsArgsIt = rhsExpr.getArguments().begin();
        auto lhsArgsItEnd = lhsExpr->getArguments().end();
        auto rhsArgsItEnd = rhsExpr.getArguments().end();
        size_t lhsNumArgs = std::distance(lhsArgsIt, lhsArgsItEnd);
        size_t rhsNumArgs = std::distance(rhsArgsIt, rhsArgsItEnd);

        if(lhsNumArgs != rhsNumArgs) {
          return lhsNumArgs < rhsNumArgs ? -1 : 1;
        }

        while(lhsArgsIt != lhsArgsItEnd /*&& rhsArgsIt != rhsArgsItEnd*/) {
          int argCompare = compare(*lhsArgsIt, *rhsArgsIt);
          if(argCompare != 0) {
            return argCompare;
          }
          ++lhsArgsIt;
          ++rhsArgsIt;
        }
      }

      // "normal" values (of identical type) are all dispatched to the same batch
      return 0;
    }
  };

  using BatchMap = std::map<DispatchKey, BatchPtr, compareDispatchKey>;
  BatchMap m_batches;

  BatchFactory const& m_factory;
  bool m_decomposed;
};

} // namespace boss::engines::bulk
