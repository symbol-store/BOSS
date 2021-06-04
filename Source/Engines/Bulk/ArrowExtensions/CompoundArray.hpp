#pragma once

#include "../../Bulk.hpp"
#include "../BatchData.hpp"
#include "../ExtendedExpression.hpp"
#include "ComplexExpressionArray.hpp"
#include "ExpressionArray.hpp"

#include "../ArrowExtensions/MutableChunkedArray.hpp"

#include "../../../Expression.hpp"

#include <string>
#include <vector>

namespace boss::engines::bulk {

template <class T> struct isSharedPtr : std::false_type {};
template <class T> struct isSharedPtr<std::shared_ptr<T>> : std::true_type {};

/** Root storage for a complex expression.
 * This class is composed of a builder and chunked arrays.
 * When we append a complex expression, it is stored to the builder
 * until we call freezeData() to finish it into an array.
 * The current behaviour, which can be disabled in append(), is to also freezeData() too
 * when a complex expression with a new signature is appended.
 * It enforces the underline union array to have a single array
 * and all the stored data to be ordered but it causes the data to be split into more chunks. */
class CompoundArray {
public:
  using ComplexExpressionArrayBuilder = ComplexExpressionArrayBuilder<ExpressionArrayBuilder>;

  CompoundArray(bool decomposed) : m_builder(nullptr), m_decomposed(decomposed) {}

  CompoundArray(arrow::ArrayVector&& argArrays, std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder,
                bool decomposed)
      : m_arrays(std::move(argArrays)),
        m_builder(
            std::move(std::dynamic_pointer_cast<ComplexExpressionArrayBuilder>(arrayBuilder))),
        m_decomposed(decomposed) {}

  CompoundArray(CompoundArray&& other) noexcept
      : m_arrays(std::move(other.m_arrays)), m_builder(std::move(other.m_builder)),
        m_decomposed(other.m_decomposed) {}

  CompoundArray(CompoundArray const& other, bool clear = false)
      : m_builder(other.m_builder != nullptr
                      ? std::make_shared<ComplexExpressionArrayBuilder>(*other.m_builder, clear)
                      : nullptr),
        m_decomposed(other.m_decomposed) {
    if(!clear) {
      m_arrays.append(other.m_arrays);
    }
  }

  CompoundArray(CompoundArray const& other, std::shared_ptr<arrow::Array> const& singleArray,
                bool decomposed)
      : m_arrays({singleArray}),
        m_builder(other.m_builder != nullptr
                      ? std::make_shared<ComplexExpressionArrayBuilder>(*other.m_builder, true)
                      : nullptr),
        m_decomposed(decomposed) {}

  CompoundArray(CompoundArray const& other, std::shared_ptr<arrow::Array>&& singleArray,
                bool decomposed)
      : m_arrays({std::move(singleArray)}),
        m_builder(other.m_builder != nullptr
                      ? std::make_shared<ComplexExpressionArrayBuilder>(*other.m_builder, true)
                      : nullptr),
        m_decomposed(decomposed) {}

  explicit CompoundArray(std::shared_ptr<ComplexExpressionArrayBuilder> const& builder,
                         bool decomposed)
      : m_builder(std::make_shared<ComplexExpressionArrayBuilder>(*builder, false)),
        m_decomposed(decomposed) {}

  explicit CompoundArray(std::shared_ptr<ComplexExpressionArrayBuilder>&& builder, bool decomposed)
      : m_builder(std::make_shared<ComplexExpressionArrayBuilder>(*builder, false)),
        m_decomposed(decomposed) {}

  ~CompoundArray() = default;
  CompoundArray& operator=(CompoundArray const& other) = delete;
  CompoundArray& operator=(CompoundArray&& other) = delete;

  bool isDecomposed() const { return m_decomposed; }

  Symbol const& getHead() const {
    if(m_builder) {
      return m_builder->getHead();
    }
    if(m_arrays.length() > 0) {
      auto type = m_arrays.chunk(0)->type();
      auto& complexArrayType =
          dynamic_cast<ComplexExpressionArray::ComplexExpressionArrayType&>(*type);
      return complexArrayType.getHead();
    }
    // TODO: should not be called on an empty array...
    // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    static Symbol dummy("");
    return dummy;
  }

  arrow::ChunkedArray const& getChunkedArray() const { return m_arrays; }

  void append(BulkComplexExpression const& expression) {
    // [https://github.com/symbol-store/BOSS/issues/92] implement later one of these methods:
    // A) supporting multiple builders to avoid splitting data too much
    // B) append all to the same builder and handle well union array with multiple fields
    // But for now, make sure to split into new chunks every time there is a new type of expression
    if(m_builder && m_builder->num_fields() > 0) {
      if(!m_builder->IsSupported(expression)) {
        freezeData();

        // re-create the builder but keep the field names (if they exist)
        arrow::FieldVector fields;
        std::string headName = m_builder->getHead().getName();
        auto type = m_builder->type();
        auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
        auto structType = extensionType.storage_type();
        fields.insert(fields.end(), structType->fields().begin(), structType->fields().end());
        fields.resize(expression.getArguments().size(), std::make_shared<arrow::Field>("", nullptr));
        m_builder = std::make_shared<ComplexExpressionArrayBuilder>(Symbol(headName), fields);
      }
    }

    if(!m_builder) {
      // initialise builder with the right arguments count at the first insert
      // if it hasn't been specified and initialised yet
      m_builder = std::make_shared<ComplexExpressionArrayBuilder>(expression.getHead(),
                                                                  expression.getArguments().size());
    }

    auto status = m_builder->AppendExpression(expression);
    if(!status.ok()) {
      // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    }
  }

  void append(Symbol const& head, std::vector<BatchData> const& argData) {
    // [https://github.com/symbol-store/BOSS/issues/92] implement later one of these methods:
    // A) supporting multiple builders to avoid splitting data too much
    // B) append all to the same builder and handle well union array with multiple fields
    // But for now, make sure to split into new chunks every time there is a new type of expression
    if(m_builder && !m_builder->IsSupported(argData)) {
      freezeData();
      // try to copy the fields (to transfer column names if it is set).
      // only if the new ones aren't empty (empty = not coming from columns)
      auto fields = childFields();
      for(int i = 0; i < argData.size(); ++i) {
        if(!fields[i] || !argData[i].field->name().empty()) {
          fields[i] = argData[i].field;
        }
      }
      m_builder = std::make_shared<ComplexExpressionArrayBuilder>(head, fields);
    }

    if(!m_builder) {
      // try to copy the fields (to transfer column names if it is set)
      arrow::FieldVector fields;
      fields.reserve(argData.size());
      for(auto const& batchData : argData) {
        fields.emplace_back(batchData.field);
      }
      m_builder = std::make_shared<ComplexExpressionArrayBuilder>(head, fields);
    }

    auto status = m_builder->AppendExpressions(argData);
    if(!status.ok()) {
      // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    }
  }

  void append(CompoundArray&& other) {
    m_arrays.append(std::move(other.m_arrays));

    if(other.m_builder && m_builder) {
      // need to finalise first the existing builder
      freezeData();
    }

    m_builder = std::move(other.m_builder);
  }

  void initArguments(Symbol const& head, std::vector<BatchData> const& argData) {
    if(!m_builder) {
      // initialise the column names too
      arrow::FieldVector fields;
      fields.reserve(argData.size());
      for(int i = 0; i < argData.size(); ++i) {
        fields.emplace_back(argData[i].field);
      }
      m_builder = std::make_shared<ComplexExpressionArrayBuilder>(head, fields);
    }

    std::vector<std::shared_ptr<arrow::DataType>> types;
    types.reserve(argData.size());
    for(auto const& batchData : argData) {
      if(batchData.builder) {
        types.emplace_back(batchData.builder->type());
      } else {
        // assuming at least one row
        auto const& chunk = batchData.arrays.chunk(0);
        types.emplace_back(chunk->type());
      }
    }

    auto status = m_builder->initArguments(types);
    if(!status.ok()) {
      // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    }
  }

  void reserve(size_t size) {
    if(m_builder && size > m_builder->capacity()) {
      auto status = m_builder->Reserve(size - m_builder->length());
      if(!status.ok()) {
        return;
      }
    }
  }

  void resize(size_t size) {
    // resize the parent array, but not the children
    if(m_builder) {
      auto status = m_builder->deepResize(size);
      if(!status.ok()) {
        return;
      }
    }
  }

  void clear() {
    m_arrays.clear();
    m_builder.reset();
  }

  void setOwner(CompoundArray const* parentArray, size_t childIndex = 0) {
    m_parentArray = parentArray;
    m_childIndex = childIndex;
  }

  BatchData data() const {
    auto builder = getBuilder();
    auto builderLength = builder ? builder->length() : 0;
    return BatchData(getChunkedArray(), std::move(builder), builderLength, field());
  }
  
  BatchData freezedData() {
    freezeData();
    return data();
  }

  void freezeData() {
    std::shared_ptr<arrow::Array> chunkArray;
    if(m_parentArray) {
      auto newChunkIndex = m_arrays.chunks().size();
      const_cast<CompoundArray*>(m_parentArray)->freezeData(); // NOLINT
      if(m_parentArray->numChunks() <= newChunkIndex) {
        // nothing new
        return;
      }
      // [https://github.com/symbol-store/BOSS/issues/92] handle multiple types in union
      chunkArray = m_parentArray->getArgument(newChunkIndex, m_childIndex)->field(0);
    } else {
      if(!m_builder || m_builder->length() == 0) {
        // nothing new
        return;
      }
      auto result = m_builder->Finish(&chunkArray);
      if(!result.ok()) {
        // failed
        return;
      }
    }
    m_arrays.append(std::move(chunkArray));
  }

  std::shared_ptr<arrow::Field> field() const {
    if(m_parentArray) {
      return m_parentArray->childField(m_childIndex);
    }
    return std::make_shared<arrow::Field>("", nullptr);
  }

  std::shared_ptr<arrow::Field> childField(size_t index) const {
    auto fields = childFields();
    if(!fields.empty()) {
      return fields[index];
    }
    return std::make_shared<arrow::Field>("", nullptr);
  }

  size_t length() const {
    if(!m_decomposed) {
      return numArguments();
    }
    return m_decomposed ? numRows() : numArguments();
  }

  // [https://github.com/symbol-store/BOSS/issues/88] ideally should not be exposed
  size_t numChunks() const { return m_arrays.num_chunks(); }

  size_t numArguments() const {
    if(m_arrays.num_chunks() > 0) {
      return m_arrays.chunk(0)->num_fields();
    }
    return m_builder ? m_builder->num_fields() : 0;
  }

  size_t numRows() const {
    if(m_builder) {
      return m_arrays.length() + m_builder->length();
    }

    return m_arrays.length();
  }

  bool hasBuilder() const { return bool(m_builder && m_builder->length() > 0); }
  std::shared_ptr<ComplexExpressionArrayBuilder> getBuilder() const { return m_builder; }

  void addArgument(std::string const& argName) {
    arrow::FieldVector fields;
    std::string headName = "List";
    if(m_builder) {
      headName = m_builder->getHead().getName();
      auto type = m_builder->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      fields.insert(fields.end(), structType->fields().begin(), structType->fields().end());
    }
    fields.emplace_back(std::make_shared<arrow::Field>(argName, nullptr));
    m_builder = std::make_shared<ComplexExpressionArrayBuilder>(Symbol(headName), fields);
  }

  // [https://github.com/symbol-store/BOSS/issues/88] ideally should not be exposed
  std::shared_ptr<ExpressionArrayBuilder> getArgumentBuilder(size_t columnIdx) const {
    if(!m_builder || m_builder->num_children() == 0) {
      return nullptr;
    }
    return std::dynamic_pointer_cast<ExpressionArrayBuilder>(m_builder->child_builder(columnIdx));
  }

  std::shared_ptr<ExpressionArray> getArgument(size_t chunkIdx, size_t columnIdx) const {
    auto& chunkArray = dynamic_cast<ComplexExpressionArray&>(*m_arrays.chunk(chunkIdx));
    return std::dynamic_pointer_cast<ExpressionArray>(chunkArray.field(columnIdx));
  }

  std::shared_ptr<arrow::ArrayData> getArrayData(size_t chunkIdx) const {
    auto& chunkArray = dynamic_cast<ComplexExpressionArray&>(*m_arrays.chunk(chunkIdx));
    return chunkArray.data();
  }

  std::shared_ptr<arrow::Array> getRow(size_t rowIdx) const {
    if(rowIdx >= m_arrays.length()) {
      // extracting row from the builder
      // need to finish it first...
      const_cast<CompoundArray*>(this)->freezeData(); // NOLINT
    }
    return m_arrays.Slice(rowIdx, 1)->chunk(0);
  }

  /// extract a "row" (which has a different meaning for decomposed or not decomposed batch)
  BulkExpression extract(size_t index) const {
    if(!m_decomposed) {
      // extract row value as single value array instead
      return column(index);
    }

    auto rowArray = getRow(index);
    return std::make_shared<CompoundArray>(*this, std::move(rowArray), false);
  }

  void addColumn(Symbol const& column) { addArgument(column.getName()); }

  /// extract a child batch (regardless of the decomposed flag)
  /// It will create a child batch from the underline arrow array
  /// (const version, returning a WritablePtr)
  BulkExpression column(size_t index) const {
    // retrieve all the array chunks from the child array
    arrow::ArrayVector argChunks;
    argChunks.reserve(numChunks());
    for(size_t chunkIdx = 0; chunkIdx < numChunks(); ++chunkIdx) {
      argChunks.emplace_back(getChildChunk(index, chunkIdx));
    }
    // + retrieve the child builder if it has been used (and not yet finished into an array)
    std::shared_ptr<arrow::ArrayBuilder> argBuilder = getChildBuilder(index);

    return Engine::createArray(std::move(argChunks), std::move(argBuilder), this, index);
  }

  template <typename Func> void visitPartitions(Func&& visitor) const {
    for(auto chunk : m_arrays.chunks()) {
      visitor(std::make_shared<CompoundArray>(*this, std::move(chunk), m_decomposed));
    }
    if(m_builder && m_builder->length() > 0) {
      visitor(std::make_shared<CompoundArray>(m_builder, m_decomposed));
    }
  }

  class ColumnIterator {
  public:
    explicit ColumnIterator(CompoundArray const& compoundArray_, size_t index_ = 0)
        : compoundArray(compoundArray_), index(index_) {}
    BulkExpression operator*() const { return compoundArray.column(index); }
    bool operator!=(ColumnIterator const& rhs) const { return index != rhs.index; }
    bool operator!=(ColumnIterator&& rhs) const { return index != rhs.index; }
    ColumnIterator operator+(size_t incr) const {
      return ColumnIterator(compoundArray, index + incr);
    }
    void operator++() { index++; }

  private:
    CompoundArray const& compoundArray;
    size_t index;
  };

  ColumnIterator begin() const { return ColumnIterator(*this); }
  ColumnIterator end() const { return ColumnIterator(*this, numArguments()); }

private:
  bool m_decomposed;

  MutableChunkedArray m_arrays;
  std::shared_ptr<ComplexExpressionArrayBuilder> m_builder;

  CompoundArray const* m_parentArray = nullptr;
  size_t m_childIndex = 0;

  arrow::FieldVector childFields() const {
    if(m_builder) {
      auto type = m_builder->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      return structType->fields();
    }
    if(m_arrays.num_chunks() > 0) {
      auto type = m_arrays.chunk(0)->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      return structType->fields();
    }
    return arrow::FieldVector{};
  }

  std::shared_ptr<arrow::Array> getChildChunk(size_t index, size_t chunkIndex) const {
    auto argArray = getArgument(chunkIndex, index);
    auto const& argArrayData = *getArrayData(chunkIndex);
    // [https://github.com/symbol-store/BOSS/issues/92]
    // handle heterogeneous arrays (returning field(1), etc)
    // but need to think about how to slice them
    return argArray->field(0)->Slice(argArrayData.offset, argArrayData.length);
  }

  std::shared_ptr<arrow::ArrayBuilder> getChildBuilder(size_t index) const {
    auto argBuilder = getArgumentBuilder(index);
    // [https://github.com/symbol-store/BOSS/issues/92]
    // handle heterogeneous arrays (returning child_builder(1), etc)
    if(argBuilder && argBuilder->num_children() > 0) {
      return argBuilder->child_builder(0);
    }
    return nullptr;
  }
};

} // namespace boss::engines::bulk
