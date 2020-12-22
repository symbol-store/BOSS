#pragma once

#include "Batch.hpp"
#include "SymbolBatch.hpp"
#include "ValueBatch.hpp"

#include "../../../Expression.hpp"

#include <memory>
#include <vector>

namespace boss::engines::bulk {

template <typename... SupportedTypes> class CompoundBatch : public Batch {
public:
  using ValueType = ComplexExpression;
  using IsRLE = std::bool_constant<false>;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<CompoundBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool isRLE() const override { return IsRLE::value; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  using BatchPtr = std::unique_ptr<Batch>;
  using BatchList = std::vector<BatchPtr>;

  CompoundBatch() : m_symbol(Symbol("_undefined")), m_batches() {}

  CompoundBatch(Symbol const& symbol, BatchList&& batches)
      : m_symbol(symbol), m_batches(std::move(batches)) {}

  CompoundBatch(CompoundBatch const& other, bool clear = false) : m_symbol(other.m_symbol) {
    m_batches.reserve(other.m_batches.size());
    for(auto const& otherBatchPtr : other.m_batches) {
      m_batches.emplace_back(std::move(otherBatchPtr.get()->clone(clear)));
    }
  }

  BatchPtr clone(bool clear = false) const override { return BatchPtr(new CompoundBatch(*this, clear)); }
  void clear() override {
    for(auto& batchPtr : m_batches) {
      batchPtr.get()->clear();
    }
  }

  BatchPtr extract(size_t index) const {
    return std::next(m_batches.begin(), index)->get()->clone();
  }

  size_t numBatches() const { return std::distance(m_batches.begin(), m_batches.end()); }

  template <typename Func> void visitBatches(Func&& visitor) const {
    visitBatches<Func>(std::move(visitor), m_batches);
  }

  template <typename... SupportedBatchTypes> class ConstIterator {
  public:
    ConstIterator(Symbol const& head, BatchList const& batchList, size_t index = 0)
        : m_head(head), m_batchList(batchList), m_index(index) {}
    ComplexExpression operator*() {
      ExpressionArguments arguments;
      arguments.reserve(m_batchList.size());
      CompoundBatch::visitBatches(
          [this, &arguments](auto const& batch) {
            using BatchType = std::decay_t<decltype(batch)>;
            using ReturnType = typename BatchType::ValueType;
            auto const& value = *(batch.begin() + m_index);
            arguments.emplace_back((ReturnType)value);
          },
          m_batchList);
      return ComplexExpression(m_head, std::move(arguments));
    }
    bool operator!=(ConstIterator& rhs) { return m_index != rhs.m_index; }
    bool operator!=(ConstIterator&& rhs) { return m_index != rhs.m_index; }
    ConstIterator operator+(size_t incr) const {
      return ConstIterator(m_head, m_batchList, m_index + incr);
    }
    void operator++() { m_index++; }

  private:
    Symbol const& m_head;
    BatchList const& m_batchList;
    size_t m_index;
  };

  template <typename... SupportedBatchTypes> struct SupportedTypeInfo {
    using ConstIteratorType = ConstIterator<SupportedBatchTypes...>;
  };

  using SupportedBatchTypeInfo =
      SupportedTypeInfo<ValueBatch<SupportedTypes>..., RLEBatch<SupportedTypes>..., SymbolBatch,
                        CompoundBatch>;

  using ConstIteratorImpl = typename SupportedBatchTypeInfo::ConstIteratorType;

  auto begin() const { return ConstIteratorImpl(m_symbol, m_batches, 0); }
  auto end() const { return ConstIteratorImpl(m_symbol, m_batches, size()); }


  size_t size() const override {
    size_t maxSize = 0;
    for(auto const& batchPtr : m_batches) {
      size_t inputSize = batchPtr.get()->size();
      if(inputSize > maxSize) {
        maxSize = inputSize;
      }
    }
    return maxSize;
  }

  void insert(Expression const& val) override {
    auto& expression = std::get<ComplexExpression>(val);
    auto argsBegin = expression.getArguments().begin();
    auto argsEnd = expression.getArguments().end();

    size_t sizeArgs = std::distance(argsBegin, argsEnd);
    if(sizeArgs > m_batches.size()) {
      if(m_batches.size() == 2) {
        // split arguments into pairs and create deeper compound expressions

        size_t numPassingOverArgs = sizeArgs - 1;

        ExpressionArguments compoundList{argsBegin, std::next(argsBegin, numPassingOverArgs)};
        ComplexExpression compoundExpr{expression.getHead(), compoundList};

        ExpressionArguments newList{compoundExpr, *std::next(argsBegin, numPassingOverArgs)};
        ComplexExpression newExpr{expression.getHead(), newList};
        insert(newExpr);
        return;
      }
    }

    auto argIt = argsBegin;
    for(size_t index = 0; index < m_batches.size(); ++index) {
      if(argIt != argsEnd) {
        insert(index, *argIt);
        ++argIt;
      }
    };
  }

  void insert(size_t index, Expression const& argument) {
    auto& batchPtr = *std::next(m_batches.begin(), index);
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
  }

  BatchPtr evaluate() const override {
    auto evaluatedPtr = clone();
    auto& evaluated = *static_cast<CompoundBatch*>(evaluatedPtr.get());
    for(auto& batchPtr : evaluated.m_batches) {
      batchPtr = batchPtr.get()->evaluate();
    }
    return evaluatedPtr;
  }

protected:
  Symbol m_symbol;
  BatchList m_batches;

  template <typename Func> static void visitBatches(Func&& visitor, BatchList const& batchList) {
    visitBatches<Func>(std::move(visitor), batchList, SupportedBatchTypeInfo{});
  }

  template <typename Func, template <typename...> typename TList, typename... Types>
  static void visitBatches(Func&& visitor, BatchList const& batchList, TList<Types...>) {
    for(auto const& batchPtr : batchList) {
      auto const& batch = *batchPtr.get();
      (..., visitHelper<std::decay_t<Func>, Types>(visitor, batch));
    }
  }

  template <typename Func, typename BatchType>
  static void visitHelper(Func& visitor, Batch const& batch) {
    if(batch.typeId() == UniqueId::forType<BatchType>()) {
      visitor(*static_cast<BatchType const*>(&batch));
    }
  }
};

} // namespace boss::engines::bulk
