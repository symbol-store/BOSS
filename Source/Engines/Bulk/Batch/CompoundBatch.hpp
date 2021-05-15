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

/** CompoundBatch is a batch composed of children batches (of any types).
 * The logical meaning fo the children batches depends on value of the 'decomposed' flag:
 * - if true: a tuple is reconstructed by taking one element from each child batch (how we store
 * database tables)
 * - if false: each child batch is a tuple, and they can be single-element batches to represent a
 * simple list. The class only logically contains the batches. They are actually created only on
 * retrieval. It physically stores only arrow arrays.
 */
class CompoundBatch : public Batch {
public:
  using ValueType = ComplexExpression;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<CompoundBatch>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  /** check if this batch is able to store this type of expression
   * we actually only check here if to store only complex expressions
   * but we don't check precisely all the arguments and the head */
  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  /// factory: used to create the children batches on insert
  /// decomposed: see class description for the meaning
  /// defaulting to create a "List" as head for the expressions we insert
  explicit CompoundBatch(BatchFactory const& factory, bool decomposed = false)
      : m_factory(factory), m_array(std::make_shared<CompoundArray>()), m_symbol("List"_),
        m_decomposed(decomposed) {}

  /// factory: used to create the children batches on insert
  /// symbol: head for the expressions we insert
  /// decomposed: see class description for the meaning
  CompoundBatch(BatchFactory const& factory, Symbol const& symbol, bool decomposed = false)
      : m_factory(factory), m_array(std::make_shared<CompoundArray>()), m_symbol(symbol),
        m_decomposed(decomposed) {}

  // just a shallow copy
  CompoundBatch(CompoundBatch const& other, bool clear = false)
      : m_factory(other.m_factory), m_array(std::make_shared<CompoundArray>(*other.m_array, clear)),
        m_symbol(other.m_symbol), m_decomposed(other.m_decomposed) {}
  // what do we do with shallow copies? Should we have a "ViewBatch" type or something to designate
  // that?

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

  /// create a full copy of the batch (without knowing the derived batch type)
  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsCompoundBatch(clear));
  }
  virtual WritableBatchPtr<CompoundBatch> cloneAsCompoundBatch(bool clear = false) const {
    return WritableBatchPtr(new CompoundBatch(*this, clear));
  }

  /// convenience function to clone a batch to a specific type
  /// it will work only with the same batch type or derived type
  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, CompoundBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsCompoundBatch(clear);
  }

  void clear() { m_array->clear(); }

  void resize(size_t size) override { m_array->resize(size); }

  size_t size() const override { return m_decomposed ? m_array->length() : numArguments(); }

  size_t numArguments() const { return m_array->numArguments(); }

  class ConstColumnIterator {
  public:
    using value_type = ReadablePtr;
    explicit ConstColumnIterator(CompoundBatch const& batch, size_t index = 0)
        : m_batch(batch), m_index(index) {}
    ReadablePtr operator*() const { return m_batch.column(m_index); }
    bool operator!=(ConstColumnIterator const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(ConstColumnIterator&& rhs) const { return m_index != rhs.m_index; }
    ConstColumnIterator operator+(size_t incr) const {
      return ConstColumnIterator(m_batch, m_index + incr);
    }
    void operator++() { m_index++; }

  private:
    CompoundBatch const& m_batch;
    size_t m_index;
  };

  auto begin() const { return ConstColumnIterator(*this); }
  auto end() const { return ConstColumnIterator(*this, numArguments()); }

  class ColumnIterator {
  public:
    using value_type = WritablePtr;
    explicit ColumnIterator(CompoundBatch& batch, size_t index = 0)
        : m_batch(batch), m_index(index) {}
    WritablePtr operator*() const { return m_batch.column(m_index); }
    bool operator!=(ColumnIterator const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(ColumnIterator&& rhs) const { return m_index != rhs.m_index; }
    ColumnIterator operator+(size_t incr) const { return ColumnIterator(m_batch, m_index + incr); }
    void operator++() { m_index++; }

  private:
    CompoundBatch& m_batch;
    size_t m_index;
  };

  auto begin() { return ColumnIterator(*this); }
  auto end() { return ColumnIterator(*this, numArguments()); }

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

  /// extract a "row" (which has a different meaning for decomposed or not decomposed batch)
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

  /// extract a child batch (regardless of the decomposed flag)
  /// It will creat a child batch from the underline arrow array
  /// (const version, returning a WritablePtr)
  virtual Batch::ReadablePtr column(size_t index) const {
    // retrieve all the array chunks from the child array
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

    // + retrieve the child builder if it has been used (and not yet finished into an array)
    std::shared_ptr<arrow::ArrayBuilder> argBuilder = m_array->getArgumentBuilder(index);
    // TODO: handle heterogeneous arrays (returning child_builder(1), etc)
    if(argBuilder) {
      argBuilder = argBuilder->child_builder(0);
    }

    // create a batch of the right type from these arrays/builder
    Batch* batch = m_factory.createBatch(std::move(argChunks), std::move(argBuilder));
    batch->setOwner(m_array, index);
    auto const* constBatch = batch;
    return Batch::ReadablePtr(constBatch);
  }

  /// extract a child batch (regardless of the decomposed flag)
  /// It will creat a child batch from the underline arrow array
  /// (non-const version, returning a ReadablePtr)
  virtual Batch::WritablePtr column(size_t index) {
    // retrieve all the array chunks from the child array
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

    // + retrieve the child builder if it has been used (and not yet finished into an array)
    std::shared_ptr<arrow::ArrayBuilder> argBuilder = m_array->getArgumentBuilder(index);
    // TODO: handle heterogeneous arrays (returning child_builder(1), etc)
    if(argBuilder) {
      argBuilder = argBuilder->child_builder(0);
    }

    // create a batch of the right type from these arrays/builder
    Batch* batch = m_factory.createBatch(std::move(argChunks), std::move(argBuilder));
    batch->setOwner(m_array, index);
    return Batch::WritablePtr(batch);
  }

  // TODO: rename to pushBack? push_back? append?
  void insert(Expression const& expression) override { insert(std::get<ValueType>(expression)); }

  void insert(ValueType const& expression) {
    auto status = m_array->append(expression);
    if(!status.ok()) {
      return;
    }
  }

  void insert(CompoundArray&& compoundArray) { m_array->merge(std::move(compoundArray)); }

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

  // [ISSUE] cleanup usage of arrow API
  void setOwner(std::shared_ptr<CompoundArray> parentArray, size_t childIndex) override {
    // used to set the owner (parent batch) after creating a child batch in CompoundBatch::column()
    // so the parent can freezeData() when the child need to freezeData()
    // since it should always be done together
    m_array->setOwner(std::move(parentArray), childIndex);
  }

private:
  BatchFactory const& m_factory;
  Symbol m_symbol;
  bool m_decomposed;

  std::shared_ptr<CompoundArray> m_array;
};

} // namespace boss::engines::bulk
