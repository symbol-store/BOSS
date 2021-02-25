#pragma once

#include "BatchFactory.hpp"
#include "BatchHelper.hpp"

#include "Batch/Batch.hpp"
#include "Batch/RLEBatch.hpp"
#include "Batch/ValueBatch.hpp"

#include "../../Expression.hpp"

#include <algorithm>
#include <map>

namespace boss::engines::bulk {

/********************* class Dispatcher ***********************/

/* insert any expression into its specific Batch type         */
/* deduced from the expression head and argument types        */
/**************************************************************/

class Dispatcher {
public:
  Dispatcher(BatchFactory const& factory, bool decomposed, bool ordered)
      : m_factory(factory), m_decomposed(decomposed), m_ordered(ordered) {}

  Dispatcher(Dispatcher const& other, bool clear = false)
      : m_factory(other.m_factory), m_decomposed(other.m_decomposed), m_ordered(other.m_ordered) {
    if(!clear) {
      for(auto const& [key, otherBatchPtr] : other.m_batches) {
        // just a shallow copy
        insert(key.first, key.second, otherBatchPtr);
      }
    }
  }

  ~Dispatcher() = default;
  Dispatcher(Dispatcher&& other) = default;
  Dispatcher& operator=(Dispatcher const& other) = delete;
  Dispatcher& operator=(Dispatcher&& other) = delete;

  bool isDecomposed() const { return m_decomposed; }
  void setDecomposed(bool value) { m_decomposed = value; }

  auto begin() const { return m_batches.begin(); }
  auto end() const { return m_batches.end(); }

  auto begin() { return m_batches.begin(); }
  auto end() { return m_batches.end(); }

  void clear() {
    m_batches.clear();
    m_indexedBatches.clear();
  }

  size_t size() const {
    if(m_decomposed) {
      return m_batches.empty() ? 0 : m_batches.begin()->second->size();
    }
    if(!m_ordered) {
      size_t totalSize = 0;
      visitBatches([&totalSize](auto const& /*key*/, auto const& batchPtr) {
        totalSize += batchPtr->size();
      });
      return totalSize;
    }
    return m_batches.size();
  }

  void resize(size_t size, Expression const& val) {
    if(m_ordered) {
      if(size < m_batches.size()) {
        BatchMap newBatches;
        for(auto& [key, batchPtr] : m_batches) {
          newBatches[key] = std::move(batchPtr);
        }
        m_batches.swap(newBatches);
        m_indexedBatches.clear();
      } else if(size > m_batches.size()) {
        size_t sizeDiff = size - m_batches.size();
        size_t indexOffset = m_batches.empty() ? 0 : 1 + m_batches.rbegin()->first.second;
        for(size_t argIndex = 0; argIndex < sizeDiff; ++argIndex) {
          insert(val, argIndex + indexOffset, m_factory.createBatch(val));
        }
        m_indexedBatches.clear();
      }
    } else {
      // TODO: implements if needed
    }
  }

  template <typename Func> void visitEvaluated(Func&& visitor) const {
    for(auto const& [argument, batchPtr] : m_batches) {
      Batch::ReadablePtr evaluatedPtr;
      if(!batchPtr->evaluate(evaluatedPtr)) {
        visitor(argument, batchPtr, false);
        continue;
      }
      auto key = m_factory.toKey(*evaluatedPtr);
      visitor(DispatchKey(key, argument.second), std::move(evaluatedPtr), true);
    }
  }

  Batch::ReadablePtr extract(Batch const& srcBatch, size_t index) const {
    if(m_decomposed) {
      // need to recompose a new row from every column
      // Notes:
      // 1. we have to go through that even if a relation has only one row (size() == 1)
      //    so it creates the right decomposed batch and avoid an infinite loop...)
      // 2. we have to go through that
      //    even if a dispatched batch has only one column (m_batches.size() == 1)
      //    otherwise we end up exporting a column as value directly instead of a list
      return m_factory.recomposeBatch(srcBatch, index, false);
    }

    refreshIndexes();

    if(m_ordered) {
      return m_indexedBatches[index].second->second;
    }

    auto nextIt = std::upper_bound(
        m_indexedBatches.begin(), m_indexedBatches.end(), index,
        [](size_t index, auto const& indexedPair) { return index < indexedPair.first; });
    auto it = std::prev(nextIt);

    size_t startIndex = it->first;
    auto& mapIt = it->second;
    auto const& batchPtr = mapIt->second;
    return m_factory.extractFromBatch(*batchPtr, index - startIndex);
  }

  Batch::ReadablePtr reduce(Batch const& srcBatch, size_t index) const {
    if(!m_ordered) {
      auto destBatchPtr = srcBatch.clone(true);
      m_factory.reduceCompoundBatch(*destBatchPtr, srcBatch, index);
      return std::move(destBatchPtr);
    }

    refreshIndexes();

    return m_indexedBatches[index].second->second;
  }

  void insert(Expression const& argument, size_t argIndex, Batch::ReadablePtr batchPtr) {
    m_indexedBatches.clear();
    DispatchKey key(argument, m_ordered ? argIndex : 0);
    auto& existingBatchPtr = m_batches[key];
    if(existingBatchPtr) {
      if(existingBatchPtr->isRLE()) {
        // need to recreate a non-RLE to be safe
        existingBatchPtr = m_factory.convertToNonRLE(std::move(existingBatchPtr));
      }
      if(batchPtr->isRLE()) {
        // and so far the other batch too need to be same type
        batchPtr = m_factory.convertToNonRLE(std::move(batchPtr));
      }
      if(existingBatchPtr->typeId() == batchPtr->typeId()) {
        auto writablePtr = Batch::WritablePtr::asWritable(existingBatchPtr);
        writablePtr->merge(std::move(batchPtr));
        existingBatchPtr = std::move(writablePtr);
      }
      return;
    }

    if(!m_ordered || m_decomposed) {
      batchPtr = m_factory.convertToDecomposed(std::move(batchPtr));
    }

    existingBatchPtr = std::move(batchPtr);
  }

  void insert(Expression const& argument, size_t argIndex = 0) {
    m_indexedBatches.clear();
    DispatchKey key(argument, m_ordered ? argIndex : 0);
    auto& batchPtr = m_batches[key];
    if(!batchPtr) {
      bool decomposedDispatch = !m_ordered || m_decomposed;
      batchPtr = m_factory.createBatch(argument, decomposedDispatch);
    }

    if(!std::holds_alternative<Symbol>(argument) &&
       !std::holds_alternative<ComplexExpression>(argument)) {
      auto const& batch = *batchPtr;
      if(batch.isRLE() && !batch.canContain(argument)) {
        // there are more than one single value
        // make it a normal value batch
        batchPtr = m_factory.convertToNonRLE(std::move(batchPtr));
      }
    }

    auto writablePtr = Batch::WritablePtr::asWritable(batchPtr);
    writablePtr->insert(argument);
    batchPtr = std::move(writablePtr);
  }

  void merge(Dispatcher const& other) {
    if(!other.m_batches.empty()) {
      m_indexedBatches.clear();
    }
    for(auto const& [argument, batchPtr] : other.m_batches) {
      insert(argument.first, argument.second, batchPtr);
    }
  }

  void merge(Dispatcher&& other) {
    if(!other.m_batches.empty()) {
      m_indexedBatches.clear();
    }
    for(auto& [argument, batchPtr] : other.m_batches) {
      insert(argument.first, argument.second, std::move(batchPtr));
    }
  }

  template <typename Func> void visitBatches(Func&& visitor) const {
    for(auto const& [argument, batchPtr] : m_batches) {
      visitor(argument, batchPtr);
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

  void refreshIndexes() const {
    if(m_indexedBatches.empty()) {
      m_indexedBatches.reserve(m_batches.size());
      // regenerate the cache
      size_t cumulatedIndex = 0;
      for(auto it = m_batches.begin(); it != m_batches.end(); ++it) {
        auto const& batchPtr = it->second;
        m_indexedBatches.emplace_back(cumulatedIndex, it);
        cumulatedIndex += batchPtr->size();
      }
    }
  }

  using BatchMap = std::map<DispatchKey, Batch::ReadablePtr, compareDispatchKey>;
  BatchMap m_batches;

  mutable std::vector<std::pair<size_t, BatchMap::const_iterator>> m_indexedBatches;

  BatchFactory const& m_factory;
  bool m_decomposed;
  bool const m_ordered;
};

} // namespace boss::engines::bulk
