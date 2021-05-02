#pragma once

#include "Batch.hpp"

#include "../BatchHelper.hpp"
#include "../SymbolPool.hpp"
#include "../Utils/CompoundArray.hpp"

#include "../../../Utilities.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace boss::engines::bulk {

using boss::utilities::operator""_;

class CompoundBatch : public Batch {
public:
  using ValueType = ComplexExpression;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<CompoundBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  explicit CompoundBatch(BatchFactory const& factory, bool decomposed = false)
      : m_factory(factory), m_array(std::make_shared<CompoundArray>()), m_symbol("List"_),
        m_decomposed(decomposed) {}

  CompoundBatch(BatchFactory const& factory, Symbol const& symbol, bool decomposed = false)
      : m_factory(factory), m_array(std::make_shared<CompoundArray>()), m_symbol(symbol),
        m_decomposed(decomposed) {}

  // just a shallow copy
  CompoundBatch(CompoundBatch const& other, bool clear = false)
      : m_factory(other.m_factory), m_array(std::make_shared<CompoundArray>(*other.m_array, clear)),
        m_symbol(other.m_symbol), m_decomposed(other.m_decomposed) {}

  CompoundBatch(BatchFactory const& factory, Symbol const& symbol, CompoundArray&& compoundArray,
                bool decomposed = false)
      : m_factory(factory), m_array(std::make_shared<CompoundArray>(std::move(compoundArray))),
        m_symbol(symbol), m_decomposed(decomposed) {}

  ~CompoundBatch() override = default;
  CompoundBatch(CompoundBatch&& other) = delete;
  CompoundBatch& operator=(CompoundBatch const& other) = delete;
  CompoundBatch& operator=(CompoundBatch&& other) = delete;

  Symbol const& getHead() const { return m_symbol; }
  void setHead(Symbol const& symbol) { m_symbol = symbol; }
  void setHead(Symbol&& symbol) { m_symbol = std::move(symbol); }

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsCompoundBatch(clear));
  }
  virtual WritableBatchPtr<CompoundBatch> cloneAsCompoundBatch(bool clear = false) const {
    return WritableBatchPtr(new CompoundBatch(*this, clear));
  }

  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, CompoundBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsCompoundBatch(clear);
  }

  void clear() override { m_array->clear(); }

  void reserve(size_t size) override { m_array->reserve(size); }

  void resize(size_t size) override { m_array->resize(size); }

  size_t size() const override { return m_decomposed ? m_array->length() : numArguments(); }

  size_t numArguments() const { return m_array->numArguments(); }

  // column iterator
  class ConstIterator {
    // finish the builder into arrays, and iterate const arrays
  public:
    using value_type = ReadablePtr;
    explicit ConstIterator(CompoundBatch const& batch, size_t index = 0)
        : m_batch(batch), m_index(index) {}
    ReadablePtr operator*() const { return m_batch.column(m_index); }
    bool operator!=(ConstIterator const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(ConstIterator&& rhs) const { return m_index != rhs.m_index; }
    ConstIterator operator+(size_t incr) const { return ConstIterator(m_batch, m_index + incr); }
    void operator++() { m_index++; }

  private:
    CompoundBatch const& m_batch;
    size_t m_index;
  };

  auto begin() const { return ConstIterator(*this); }
  auto end() const { return ConstIterator(*this, numArguments()); }

  class MutableIterator {
    // don't try to finish the builder, so they are still writable
    // but shouldn't try to const-iterate the columns! it would be a problem if they are
    // individually freezed.
    // TODO: need a more robust API
  public:
    using value_type = WritablePtr;
    explicit MutableIterator(CompoundBatch& batch, size_t index = 0)
        : m_batch(batch), m_index(index) {}
    WritablePtr operator*() const { return m_batch.column(m_index); }
    bool operator!=(MutableIterator const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(MutableIterator&& rhs) const { return m_index != rhs.m_index; }
    MutableIterator operator+(size_t incr) const {
      return MutableIterator(m_batch, m_index + incr);
    }
    void operator++() { m_index++; }

  private:
    CompoundBatch& m_batch;
    size_t m_index;
  };

  auto begin() { return MutableIterator(*this); }
  auto end() { return MutableIterator(*this, numArguments()); }

  template <typename Func> void visitBatches(Func&& visitor) const {
    for(auto const& batchPtr : *this) {
      visitor(batchPtr);
    }
  }

  template <typename BatchHelper, typename Func> void visitBatches(Func&& visitor) const {
    for(auto const& batchPtr : *this) {
      BatchHelper::visit([&visitor](auto const& batch) { visitor(batch); }, *batchPtr);
    }
  }

  virtual Batch::ReadablePtr extract(size_t index) const {
    if(!m_decomposed) {
      // extract row value as single value array instead
      return column(index);
    }

    auto rowArray = m_array->getRow(index);
    CompoundArray compoundRow(*m_array, std::move(rowArray));
    auto* batch = new CompoundBatch(m_factory, m_symbol, std::move(compoundRow));
    batch->m_decomposed = false;
    auto const* constBatch = batch;
    return Batch::ReadablePtr(constBatch);
  }

  virtual Batch::ReadablePtr column(size_t index) const {
    arrow::ArrayVector argChunks;
    argChunks.reserve(m_array->numChunks());
    for(size_t chunkIdx = 0; chunkIdx < m_array->numChunks(); ++chunkIdx) {
      auto argArray = m_array->getArgument(chunkIdx, index);
      auto const& argArrayData = *argArray->data();
      // TODO: handle heterogeneous arrays (returning field(1), etc)
      // but need to think about how to slice them
      auto argArrayTyped = argArray->field(0)->Slice(argArrayData.offset, argArrayData.length);
      argChunks.emplace_back(std::move(argArrayTyped));
    }

    std::shared_ptr<arrow::ArrayBuilder> argBuilder = m_array->getArgumentBuilder(index);
    // TODO: handle heterogeneous arrays (returning child_builder(1), etc)
    if(argBuilder) {
      argBuilder = argBuilder->child_builder(0);
    }

    Batch* batch = m_factory.createBatch(std::move(argChunks), std::move(argBuilder));
    batch->setOwner(m_array, index);
    auto const* constBatch = batch;
    return Batch::ReadablePtr(constBatch);
  }

  virtual Batch::WritablePtr column(size_t index) {
    arrow::ArrayVector argChunks;
    argChunks.reserve(m_array->numChunks());
    for(size_t chunkIdx = 0; chunkIdx < m_array->numChunks(); ++chunkIdx) {
      auto argArray = m_array->getArgument(chunkIdx, index);
      auto const& argArrayData = *argArray->data();
      // TODO: handle heterogeneous arrays (returning field(1), etc)
      // but need to think about how to slice them
      auto argArrayTyped = argArray->field(0)->Slice(argArrayData.offset, argArrayData.length);
      argChunks.emplace_back(std::move(argArrayTyped));
    }

    std::shared_ptr<arrow::ArrayBuilder> argBuilder = m_array->getArgumentBuilder(index);
    // TODO: handle heterogeneous arrays (returning child_builder(1), etc)
    if(argBuilder) {
      argBuilder = argBuilder->child_builder(0);
    }

    Batch* batch = m_factory.createBatch(std::move(argChunks), std::move(argBuilder));
    batch->setOwner(m_array, index);
    return Batch::WritablePtr(batch);
  }

  void insert(Expression const& expression) override { insert(std::get<ValueType>(expression)); }

  virtual void insert(ValueType const& expression) {
    auto status = m_array->append(expression);
    if(!status.ok()) {
      return;
    }
  }

  void initArguments(std::vector<ReadablePtr> const& argBatches) {
    std::vector<BatchData> m_argData;
    m_argData.reserve(argBatches.size());
    for(const auto& batchPtr : argBatches) {
      m_argData.emplace_back(batchPtr->data());
    }
    auto status = m_array->initArguments(m_symbol, m_argData);
    if(!status.ok()) {
      return;
    }
  }

  void insert(std::vector<ReadablePtr> const& argBatches) {
    std::vector<BatchData> m_argData;
    m_argData.reserve(argBatches.size());
    for(const auto& batchPtr : argBatches) {
      m_argData.emplace_back(batchPtr->data());
    }
    auto status = m_array->append(m_symbol, m_argData);
    if(!status.ok()) {
      return;
    }
  }

  void insert(CompoundArray&& compoundArray) { m_array->merge(std::move(compoundArray)); }

  bool evaluate(ReadablePtr& outputPtr) const override {
    // set the local tuple to be accessible by the row values
    auto& symbolPtr = DefaultSymbolPool::instance().findSymbol(Symbol("$tuple"));
    auto backupSymbol = std::move(symbolPtr);
    symbolPtr = Batch::ReadablePtr(shared_from_this());

    bool anyEvaluated = false;

    std::vector<Batch::ReadablePtr> argBatches;
    argBatches.reserve(numArguments());
    for(auto const& batchPtr : *this) {
      Batch::ReadablePtr evaluatedPtr;
      if(!batchPtr->evaluate(evaluatedPtr)) {
        argBatches.emplace_back(batchPtr);
      } else {
        argBatches.emplace_back(evaluatedPtr);
        anyEvaluated = true;
      }
    }

    // reset to any previous local tuple symbol
    symbolPtr = std::move(backupSymbol);

    if(!anyEvaluated) {
      outputPtr.reset();
      return false;
    }

    auto newCompoundPtr = cloneAsCompoundBatch(true);
    newCompoundPtr->insert(argBatches);
    outputPtr = std::move(newCompoundPtr);
    return true;
  }

  BatchData data() const override {
    auto builder = m_array->getBuilder();
    auto builderLength = builder->length();
    return BatchData(m_array->getChunkedArray(), std::move(builder), builderLength);
  }

  void setOwner(std::shared_ptr<CompoundArray> parentArray, size_t childIndex) override {
    m_array->setOwner(std::move(parentArray), childIndex);
  }

private:
  BatchFactory const& m_factory;
  Symbol m_symbol;
  bool m_decomposed;

  std::shared_ptr<CompoundArray> m_array;
};

} // namespace boss::engines::bulk
