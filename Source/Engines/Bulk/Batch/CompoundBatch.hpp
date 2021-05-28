#pragma once

#include "Batch.hpp"

#include "../../Bulk.hpp"
#include "../ArrowExtensions/CompoundArray.hpp"
#include "../SymbolRegistry.hpp"
#include "SymbolBatch.hpp"
#include "ValueBatch.hpp"

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

  /// decomposed: see class description for the meaning
  /// defaulting to create a "List" as head for the expressions we insert
  explicit CompoundBatch(bool decomposed = false)
      : m_array(std::make_shared<CompoundArray>()), m_symbol("List"_), m_decomposed(decomposed) {}

  /// symbol: head for the expressions we insert
  /// decomposed: see class description for the meaning
  explicit CompoundBatch(Symbol const& symbol, bool decomposed = false)
      : m_array(std::make_shared<CompoundArray>()), m_symbol(symbol), m_decomposed(decomposed) {}

  // [https://github.com/symbol-store/BOSS/issues/88] to clean up
  // Internally, we do just a shallow copy for now (see in CompoundArray copy
  // constructor) but it really should be a deep copy or it can cause some issues at some point
  CompoundBatch(CompoundBatch const& other, bool clear = false)
      : m_array(std::make_shared<CompoundArray>(*other.m_array, clear)), m_symbol(other.m_symbol),
        m_decomposed(other.m_decomposed) {}

  CompoundBatch(Symbol const& symbol, CompoundArray&& compoundArray, bool decomposed = false)
      : m_array(std::make_shared<CompoundArray>(std::move(compoundArray))), m_symbol(symbol),
        m_decomposed(decomposed) {}

  ~CompoundBatch() override = default;
  CompoundBatch(CompoundBatch&& other) = delete;
  CompoundBatch& operator=(CompoundBatch const& other) = delete;
  CompoundBatch& operator=(CompoundBatch&& other) = delete;

  Symbol const& getHead() const { return m_symbol; }

  bool isDecomposed() const { return m_decomposed; }

  /// create a full copy of the batch (without knowing the derived batch type)
  Batch* clone(bool clear = false) const override { return cloneAsCompoundBatch(clear); }

  virtual CompoundBatch* cloneAsCompoundBatch(bool clear = false) const {
    return new CompoundBatch(*this, clear);
  }

  /// convenience function to clone a batch to a specific type
  /// it will work only with the same batch type or derived type
  template <typename BatchType,
            std::enable_if_t<std::is_base_of_v<BatchType, CompoundBatch>, int> = 0>
  BatchType* cloneAs(bool clear = false) const {
    return cloneAsCompoundBatch(clear);
  }

  void clear() { m_array->clear(); }

  void resize(size_t size) override { m_array->resize(size); }

  size_t size() const override { return m_decomposed ? m_array->length() : numArguments(); }

  size_t numArguments() const { return m_array->numArguments(); }

  class ConstColumnIterator {
  public:
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

  template <typename BatchVisitDispatcher, typename Func> void visitBatches(Func&& visitor) const {
    for(auto const& batchPtr : *this) {
      BatchVisitDispatcher::visit([&visitor](auto const& batch) { visitor(batch); }, *batchPtr);
    }
  }

  template <typename Func> void visitChunks(Func&& visitor) {
    auto chunks = m_array->getChunkedArray().chunks();
    for(auto chunk : chunks) {
      CompoundArray compoundChunk(*m_array, std::move(chunk));
      auto* batch = cloneAsCompoundBatch(true);
      batch->append(std::move(compoundChunk));
      visitor(WritableBatchPtr<CompoundBatch>(batch));
    }
    if(m_array->hasBuilder()) {
      CompoundArray compoundChunk(m_array->getBuilder());
      auto* batch = cloneAsCompoundBatch(true);
      batch->append(std::move(compoundChunk));
      visitor(WritableBatchPtr<CompoundBatch>(batch));
    }
  }

  template <typename Func> void visitChunks(Func&& visitor) const {
    auto chunks = m_array->getChunkedArray().chunks();
    for(auto chunk : chunks) {
      CompoundArray compoundChunk(*m_array, std::move(chunk));
      auto* batch = cloneAsCompoundBatch(true);
      batch->append(std::move(compoundChunk));
      auto const* constBatch = batch;
      visitor(ReadableBatchPtr<CompoundBatch>(constBatch));
    }
    if(m_array->hasBuilder()) {
      CompoundArray compoundChunk(m_array->getBuilder());
      auto* batch = cloneAsCompoundBatch(true);
      batch->append(std::move(compoundChunk));
      auto const* constBatch = batch;
      visitor(ReadableBatchPtr<CompoundBatch>(constBatch));
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
    auto* batch = new CompoundBatch(m_symbol, std::move(compoundRow));
    auto const* constBatch = batch;
    return Batch::ReadablePtr(constBatch);
  }

  /// extract a child batch (regardless of the decomposed flag)
  /// It will create a child batch from the underline arrow array
  /// (const version, returning a WritablePtr)
  virtual Batch::ReadablePtr column(size_t index) const {
    // retrieve all the array chunks from the child array
    arrow::ArrayVector argChunks;
    argChunks.reserve(m_array->numChunks());
    for(size_t chunkIdx = 0; chunkIdx < m_array->numChunks(); ++chunkIdx) {
      argChunks.emplace_back(getChunk(index, chunkIdx));
    }
    // + retrieve the child builder if it has been used (and not yet finished into an array)
    std::shared_ptr<arrow::ArrayBuilder> argBuilder = getBuilder(index);

    // create a batch of the right type from these arrays/builder
    Batch* batch = createBatch(std::move(argChunks), std::move(argBuilder));
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
      argChunks.emplace_back(getChunk(index, chunkIdx));
    }
    // + retrieve the child builder if it has been used (and not yet finished into an array)
    std::shared_ptr<arrow::ArrayBuilder> argBuilder = getBuilder(index);

    // create a batch of the right type from these arrays/builder
    Batch* batch = createBatch(std::move(argChunks), std::move(argBuilder));
    batch->setOwner(m_array, index);
    return Batch::WritablePtr(batch);
  }

  void append(Expression const& expression) override { append(std::get<ValueType>(expression)); }

  void append(ValueType const& expression) {
    auto status = m_array->append(expression);
    if(!status.ok()) {
      return;
    }
  }

  void append(CompoundArray&& compoundArray) { m_array->append(std::move(compoundArray)); }

  void append(std::vector<ReadablePtr> const& argBatches) {
    std::vector<BatchData> argData;
    argData.reserve(argBatches.size());
    for(const auto& batchPtr : argBatches) {
      argData.emplace_back(batchPtr->data());
    }
    auto status = m_array->append(m_symbol, argData);
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

  bool evaluate(ReadablePtr& outputPtr) const override {
    // set the local tuple to be accessible by the row values
    auto& symbolPtr = DefaultSymbolRegistry::instance().findSymbol(Symbol("$tuple"));
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

    auto* newCompoundBatch = cloneAsCompoundBatch(true);
    newCompoundBatch->append(argBatches);
    outputPtr = WritableBatchPtr(newCompoundBatch);
    return true;
  }

  BatchData data() const override {
    auto builder = m_array->getBuilder();
    auto builderLength = builder ? builder->length() : 0;
    return BatchData(m_array->getChunkedArray(), std::move(builder), builderLength,
                     m_array->field());
  }

  // [https://github.com/symbol-store/BOSS/issues/88]
  void setOwner(std::shared_ptr<CompoundArray> parentArray, size_t childIndex) override {
    // used to set the owner (parent batch) after creating a child batch in CompoundBatch::column()
    // so the parent can freezeData() when the child need to freezeData()
    // since it should always be done together
    m_array->setOwner(std::move(parentArray), childIndex);
  }

  void addColumn(Symbol const& column) { m_array->addArgument(m_symbol, column.getName()); }

  Batch::WritablePtr columns() const {
    // create a temporary column batch (compound) from the array fields
    auto const& batchData = CompoundBatch::data();
    ExpressionArguments columns;
    if(batchData.builder || !batchData.arrays.chunks().empty()) {
      auto type = batchData.builder ? batchData.builder->type() : batchData.arrays.chunk(0)->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      columns.reserve(structType->num_fields());
      for(auto const& field : structType->fields()) {
        columns.emplace_back(Symbol(field->name()));
      }
    }
    ComplexExpression columnList("List"_, columns);
    auto columnBatch = Batch::WritablePtr(new CompoundBatch(columnList.getHead()));
    columnBatch->append(columnList);
    return columnBatch;
  }

  size_t numColumns() const {
    auto const& data = CompoundBatch::data();
    if(data.builder) {
      return data.builder->num_children();
    }
    if(!data.arrays.chunks().empty()) {
      return data.arrays.chunk(0)->num_fields();
    }
    return 0;
  }

private:
  std::shared_ptr<arrow::Array> getChunk(size_t index, size_t chunkIndex) const {
    auto argArray = m_array->getArgument(chunkIndex, index);
    auto const& argArrayData = *m_array->getArrayData(chunkIndex);
    // [https://github.com/symbol-store/BOSS/issues/92]
    // handle heterogeneous arrays (returning field(1), etc)
    // but need to think about how to slice them
    return argArray->field(0)->Slice(argArrayData.offset, argArrayData.length);
  }

  std::shared_ptr<arrow::ArrayBuilder> getBuilder(size_t index) const {
    auto argBuilder = m_array->getArgumentBuilder(index);
    // [https://github.com/symbol-store/BOSS/issues/92]
    // handle heterogeneous arrays (returning child_builder(1), etc)
    if(argBuilder && argBuilder->num_children() > 0) {
      return argBuilder->child_builder(0);
    }
    return nullptr;
  }

  /// consume a pair of (array vector, builder) to create a batch from them
  static Batch* createBatch(arrow::ArrayVector&& arrays,
                            std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder) {
    auto type = arrayBuilder ? arrayBuilder->type() : arrays[0]->type();
    // assuming all arrays and builder share the same type!
    // if not, keep only the latest type
    auto arrayIt = arrays.rbegin();
    for(; arrayIt != arrays.rend(); ++arrayIt) {
      if((*arrayIt)->type_id() != type->id()) {
        break;
      }
    }
    if(arrayIt != arrays.rend()) {
      // shrinked without the arrays having a different type
      arrow::ArrayVector shrinkedArrays(arrayIt.base(), arrays.end());
      arrays.swap(shrinkedArrays);
    }

    switch(type->id()) {
    case arrow::Type::BOOL:
      return new ValueBatch<bool>(std::move(arrays), std::move(arrayBuilder));
    case arrow::Type::INT32:
      return new ValueBatch<int>(std::move(arrays), std::move(arrayBuilder));
    case arrow::Type::FLOAT:
      return new ValueBatch<float>(std::move(arrays), std::move(arrayBuilder));
    case arrow::Type::STRING:
      return new ValueBatch<std::string>(std::move(arrays), std::move(arrayBuilder));
    case arrow::Type::EXTENSION: {
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      if(extensionType.extension_name()[0] == 's') {
        // SYMBOL
        return new SymbolBatch(std::move(arrays), std::move(arrayBuilder));
      }
      // COMPLEX EXPRESSION
      auto const& complexType =
          dynamic_cast<ComplexExpressionArray::ComplexExpressionArrayType const&>(extensionType);
      auto* batch = new CompoundBatch(complexType.getHead());
      batch->append(CompoundArray(std::move(arrays), std::move(arrayBuilder)));
      return batch;
    }
    default:
      break;
    }

    // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    return nullptr; // should not happen!
  }

  Symbol m_symbol;
  bool m_decomposed;

  std::shared_ptr<CompoundArray> m_array;
};

} // namespace boss::engines::bulk
