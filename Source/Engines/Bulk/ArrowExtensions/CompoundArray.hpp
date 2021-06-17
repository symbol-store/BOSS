#pragma once

#include "../../Bulk.hpp"
#include "../ArrayData.hpp"
#include "../BulkExpression.hpp"
#include "ComplexExpressionArray.hpp"
#include "ExpressionArray.hpp"
#include "MutableChunkedArray.hpp"
#include "ValueArrayTypes.hpp"

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

  explicit CompoundArray(bool decomposed) : builder(nullptr), decomposed(decomposed) {}

  CompoundArray(arrow::ArrayVector&& argArrays, std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder,
                bool decomposed)
      : arrays(std::move(argArrays)),
        builder(std::move(std::dynamic_pointer_cast<ComplexExpressionArrayBuilder>(arrayBuilder))),
        decomposed(decomposed) {}

  // clear: if true, we create only the column structure but not copying the data
  CompoundArray(CompoundArray const& other, bool clear = false)
      : builder(other.builder != nullptr
                    ? std::make_shared<ComplexExpressionArrayBuilder>(*other.builder, clear)
                    : nullptr),
        decomposed(other.decomposed) {
    if(!clear) {
      arrays.append(other.arrays);
    }
  }

  CompoundArray(CompoundArray&& other) noexcept = default;

  ~CompoundArray() = default;
  CompoundArray& operator=(CompoundArray const& other) = delete;
  CompoundArray& operator=(CompoundArray&& other) = delete;

  bool isDecomposed() const { return decomposed; }

  Symbol const& getHead() const {
    if(builder) {
      return builder->getHead();
    }
    if(arrays.length() > 0) {
      auto type = arrays.chunk(0)->type();
      auto& complexArrayType =
          dynamic_cast<ComplexExpressionArray::ComplexExpressionArrayType&>(*type);
      return complexArrayType.getHead();
    }
    // TODO: should not be called on an empty array...
    // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    static Symbol dummy("");
    return dummy;
  }

  void append(BulkComplexExpression const& expression) {
    // [https://github.com/symbol-store/BOSS/issues/92] implement later one of these methods:
    // A) supporting multiple builders to avoid splitting data too much
    // B) append all to the same builder and handle well union array with multiple fields
    // But for now, make sure to split into new chunks every time there is a new type of expression
    if(builder && builder->num_fields() > 0) {
      if(!builder->IsSupported(expression)) {
        freezeData();

        // re-create the builder but keep the field names (if they exist)
        arrow::FieldVector fields;
        std::string headName = builder->getHead().getName();
        auto type = builder->type();
        auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
        auto structType = extensionType.storage_type();
        fields.insert(fields.end(), structType->fields().begin(), structType->fields().end());
        fields.resize(expression.getArguments().size(),
                      std::make_shared<arrow::Field>("", nullptr));
        builder = std::make_shared<ComplexExpressionArrayBuilder>(Symbol(headName), fields);
      }
    }

    if(!builder) {
      // initialise builder with the right arguments count at the first insert
      // if it hasn't been specified and initialised yet
      builder = std::make_shared<ComplexExpressionArrayBuilder>(expression.getHead(),
                                                                expression.getArguments().size());
    }

    auto status = builder->AppendExpression(expression);
    if(!status.ok()) {
      // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    }
  }

  void append(CompoundArray&& other) {
    arrays.append(std::move(other.arrays));

    if(other.builder && builder) {
      // need to finalise first the existing builder
      freezeData();
    }

    builder = std::move(other.builder);
  }

  void append(Symbol const& head, std::vector<ArrayData> const& argData) {
    // [https://github.com/symbol-store/BOSS/issues/92] implement later one of these methods:
    // A) supporting multiple builders to avoid splitting data too much
    // B) append all to the same builder and handle well union array with multiple fields
    // But for now, make sure to split into new chunks every time there is a new type of expression
    if(builder && !builder->IsSupported(argData)) {
      freezeData();
      // try to copy the fields (to transfer column names if it is set).
      // only if the new ones aren't empty (empty = not coming from columns)
      auto fields = childFields();
      for(int i = 0; i < argData.size(); ++i) {
        if(!fields[i] || !argData[i].field->name().empty()) {
          fields[i] = argData[i].field;
        }
      }
      builder = std::make_shared<ComplexExpressionArrayBuilder>(head, fields);
    }

    if(!builder) {
      // initialise the column names too
      arrow::FieldVector fields;
      fields.reserve(argData.size());
      for(auto const& ArrayData : argData) {
        fields.emplace_back(ArrayData.field);
      }
      builder = std::make_shared<ComplexExpressionArrayBuilder>(head, fields);
    }

    auto status = builder->AppendExpressions(argData);
    if(!status.ok()) {
      // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    }
  }

  void initArguments(Symbol const& head, std::vector<ArrayData> const& argData) {
    // [https://github.com/symbol-store/BOSS/issues/92] implement later one of these methods:
    // A) supporting multiple builders to avoid splitting data too much
    // B) append all to the same builder and handle well union array with multiple fields
    // But for now, make sure to split into new chunks every time there is a new type of expression
    if(builder && !builder->IsSupported(argData)) {
      freezeData();
      // try to copy the fields (to transfer column names if it is set).
      // only if the new ones aren't empty (empty = not coming from columns)
      auto fields = childFields();
      for(int i = 0; i < argData.size(); ++i) {
        if(!fields[i] || !argData[i].field->name().empty()) {
          fields[i] = argData[i].field;
        }
      }
      builder = std::make_shared<ComplexExpressionArrayBuilder>(head, fields);
    }

    if(!builder) {
      // initialise the column names too
      arrow::FieldVector fields;
      fields.reserve(argData.size());
      for(int i = 0; i < argData.size(); ++i) {
        fields.emplace_back(argData[i].field);
      }
      builder = std::make_shared<ComplexExpressionArrayBuilder>(head, fields);
    }

    std::vector<std::shared_ptr<arrow::DataType>> types;
    types.reserve(argData.size());
    for(auto const& ArrayData : argData) {
      if(ArrayData.builder) {
        types.emplace_back(ArrayData.builder->type());
      } else {
        // assuming at least one row
        auto const& chunk = ArrayData.arrays.chunk(0);
        types.emplace_back(chunk->type());
      }
    }

    auto status = builder->initArguments(types);
    if(!status.ok()) {
      // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
    }
  }

  void reserve(size_t size) {
    if(builder && size > builder->capacity()) {
      auto status = builder->Reserve(size - builder->length());
      if(!status.ok()) {
        return;
      }
    }
  }

  void resize(size_t size) {
    if(builder) {
      auto status = builder->deepResize(size);
      if(!status.ok()) {
        return;
      }
    }
  }

  void clear() {
    arrays.clear();
    builder.reset();
  }

  void setParent(CompoundArray const* parentArray, size_t childIndex = 0) {
    this->parentArray = parentArray;
    this->childIndex = childIndex;
  }

  /// used to keep track of the global position when extracting a partition
  void setGlobalPosition(CompoundArray const* globalArray, size_t globalIndex) {
    this->globalArray = globalArray;
    this->globalIndex = globalIndex;
  }

  /// get the array at the top of the parent hierarchy
  /// (to retrieve the table/query result)
  CompoundArray const* getGlobalArray() const {
    if(parentArray == nullptr) {
      return globalArray;
    }
    return parentArray->getGlobalArray();
  }

  /// get the index of the column where this array belongs
  int getGlobalColumnIndex() const {
    if(parentArray == nullptr) {
      return -1;
    }
    int index = parentArray->getGlobalColumnIndex();
    return index < 0 ? static_cast<int>(childIndex) : index;
  }

  /// get the row index for the beginning of this array
  /// at the top of the parent hierarchy
  size_t getGlobalRowIndex() const {
    if(parentArray == nullptr) {
      return globalIndex;
    }
    return parentArray->getGlobalRowIndex();
  }

  ArrayData data() const { return ArrayData(arrays, builder, getField()); }

  ArrayData freezedData() {
    freezeData();
    return data();
  }

  void freezeData() {
    std::shared_ptr<arrow::Array> chunkArray;
    if(parentArray != nullptr) {
      auto newChunkIndex = arrays.chunks().size();
      const_cast<CompoundArray*>(parentArray)->freezeData(); // NOLINT
      if(parentArray->numChunks() <= newChunkIndex) {
        // nothing new
        return;
      }
      // [https://github.com/symbol-store/BOSS/issues/92] handle multiple types in union
      chunkArray = parentArray->getArgument(newChunkIndex, childIndex)->field(0);
    } else {
      if(!builder || builder->length() == 0) {
        // nothing new
        return;
      }
      auto result = builder->Finish(&chunkArray);
      if(!result.ok()) {
        // failed
        return;
      }
    }
    arrays.append(std::move(chunkArray));
  }

  std::shared_ptr<arrow::Field> getChildField(size_t index) const {
    auto fields = childFields();
    if(!fields.empty()) {
      return fields[index];
    }
    return std::make_shared<arrow::Field>("", nullptr);
  }

  arrow::FieldVector childFields() const {
    if(builder) {
      auto type = builder->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      return structType->fields();
    }
    if(arrays.num_chunks() > 0) {
      auto type = arrays.chunk(0)->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      return structType->fields();
    }
    return arrow::FieldVector{};
  }

  size_t length() const { return decomposed ? numRows() : numArguments(); }

  // [https://github.com/symbol-store/BOSS/issues/88] ideally should not be exposed
  size_t numChunks() const { return arrays.num_chunks(); }

  size_t numArguments() const {
    if(arrays.num_chunks() > 0) {
      return arrays.chunk(0)->num_fields();
    }
    return builder ? builder->num_fields() : 0;
  }

  size_t numRows() const {
    if(builder) {
      return arrays.length() + builder->length();
    }

    return arrays.length();
  }

  // [https://github.com/symbol-store/BOSS/issues/88] ideally should not be exposed
  std::shared_ptr<ExpressionArray> getArgument(size_t chunkIdx, size_t columnIdx) const {
    auto& chunkArray = dynamic_cast<ComplexExpressionArray&>(*arrays.chunk(chunkIdx));
    return std::dynamic_pointer_cast<ExpressionArray>(chunkArray.field(columnIdx));
  }

  std::shared_ptr<arrow::Array> getRow(size_t rowIdx) const {
    if(rowIdx >= arrays.length()) {
      // extracting row from the builder
      // need to finish it first...
      const_cast<CompoundArray*>(this)->freezeData(); // NOLINT
    }
    return arrays.Slice(rowIdx, 1)->chunk(0);
  }

  /// extract a "row" (which has a different meaning for decomposed or not decomposed array)
  BulkExpression extract(size_t index) const {
    if(!decomposed) {
      // extract as a value array (or a scalar) instead
      return column(index, true);
    }

    return std::make_shared<CompoundArray>(arrow::ArrayVector{getRow(index)}, nullptr, false);
  }

  void addColumn(Symbol const& column) { addArgument(column.getName()); }

  /// extract a child array (regardless of the decomposed flag)
  /// It will create a child array from the underline arrow array
  BulkExpression column(size_t index, bool allowScalar = false) const {
    // retrieve all the array chunks from the child array
    arrow::ArrayVector argChunks;
    argChunks.reserve(numChunks());
    for(size_t chunkIdx = 0; chunkIdx < numChunks(); ++chunkIdx) {
      argChunks.emplace_back(getChildChunk(index, chunkIdx));
    }
    // + retrieve the child builder if it has been used (and not yet finished into an array)
    std::shared_ptr<arrow::ArrayBuilder> argBuilder = getChildBuilder(index);

    if(allowScalar) {
      return Engine::createArrayOrScalar(std::move(argChunks), std::move(argBuilder), this, index);
    }
    return Engine::createArray(std::move(argChunks), std::move(argBuilder), this, index);
  }

  template <typename Func> void visitPartitions(Func&& visitor) const {
    size_t globalIndex = 0;
    for(auto const& chunk : arrays.chunks()) {
      auto partition =
          std::make_shared<CompoundArray>(arrow::ArrayVector{chunk}, nullptr, decomposed);
      partition->setGlobalPosition(this, globalIndex);
      visitor(std::move(partition));
      globalIndex += chunk->length();
    }
    if(builder && builder->length() > 0) {
      auto partition = std::make_shared<CompoundArray>(arrow::ArrayVector{}, builder, decomposed);
      partition->setGlobalPosition(this, globalIndex);
      visitor(std::move(partition));
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

  /** An iterator to go through the values in a heterogeneous column
   * with the ability to retrieve only a certain type
   * and skip the values of other types. */
  template <typename T> class IntraColumnIterator {
  private:
    using CorrectArrayType = typename ValueArrayTypeToArrowType<T>::arrayType;
    using CorrectBuilderType = typename ValueArrayTypeToArrowType<T>::builderType;

  public:
    IntraColumnIterator(arrow::ArrayVector&& arrays,
                        std::shared_ptr<arrow::ArrayBuilder> const& builder, size_t chunkIndex = 0,
                        size_t rowIndex = 0)
        : IntraColumnIterator(std::move(arrays), builder, chunkIndex,
                              std::make_shared<std::vector<bool>>(), rowIndex) {
      // cache the condition if the array type matching the type T
      // so we can faster at iterating and returning the values
      correctTypes.reserve(this->arrays.size() + 1);
      for(auto const& arrayPtr : this->arrays) {
        correctTypes.emplace_back(arrayPtr->type_id() == ValueArrayTypeToArrowType<T>::arrowTypeId);
      }
      if(builder) {
        correctTypes.emplace_back(builder->type()->id() ==
                                  ValueArrayTypeToArrowType<T>::arrowTypeId);
      } else {
        correctTypes.emplace_back(false);
      }
    }
    IntraColumnIterator(arrow::ArrayVector&& arrays,
                        std::shared_ptr<arrow::ArrayBuilder> const& builder, size_t chunkIndex,
                        std::shared_ptr<std::vector<bool>> const& correctTypesPtr,
                        size_t rowIndex = 0)
        : correctTypesPtr(correctTypesPtr),
          arraysPtr(std::make_shared<arrow::ArrayVector>(std::move(arrays))), builder(builder),
          chunkIndex(chunkIndex), rowIndex(rowIndex), correctTypes(*correctTypesPtr),
          arrays(*arraysPtr) {}
    IntraColumnIterator(std::shared_ptr<arrow::ArrayVector> const& arraysPtr,
                        std::shared_ptr<arrow::ArrayBuilder> const& builder, size_t chunkIndex,
                        std::shared_ptr<std::vector<bool>> const& correctTypesPtr,
                        size_t rowIndex = 0)
        : correctTypesPtr(correctTypesPtr), arraysPtr(arraysPtr), builder(builder),
          chunkIndex(chunkIndex), rowIndex(rowIndex), correctTypes(*correctTypesPtr),
          arrays(*arraysPtr) {}

    T operator*() const { // this operator should be called only for a matching type!
      // TODO: check how expensive is this condition when using in a loop
      if(chunkIndex >= arrays.size()) {
        return T(*(static_cast<CorrectBuilderType const*>(builder.get())->begin() + rowIndex));
      }
      if constexpr(std::is_same_v<T, Symbol> || std::is_same_v<T, std::string>) {
        return T(std::string(
            static_cast<CorrectArrayType const*>(arrays[chunkIndex].get())->GetView(rowIndex)));
      } else {
        return static_cast<CorrectArrayType const*>(arrays[chunkIndex].get())->Value(rowIndex);
      }
    }
    bool operator!=(IntraColumnIterator const& rhs) const {
      return chunkIndex != rhs.chunkIndex || rowIndex != rhs.rowIndex;
    }
    bool operator!=(IntraColumnIterator&& rhs) const {
      return chunkIndex != rhs.chunkIndex || rowIndex != rhs.rowIndex;
    }
    IntraColumnIterator operator+(size_t incr) const {
      size_t nextChunkIndex = chunkIndex;
      size_t nextRowIndex = rowIndex + incr;
      while(nextChunkIndex < arrays.size() && nextRowIndex >= arrays[nextChunkIndex]->length()) {
        nextRowIndex -= arrays[nextChunkIndex]->length();
        ++nextChunkIndex;
      }
      return IntraColumnIterator(arraysPtr, builder, nextChunkIndex, correctTypesPtr, nextRowIndex);
    }
    IntraColumnIterator operator-(size_t decr) const {
      size_t prevChunkIndex = chunkIndex;
      int prevRowIndex = rowIndex - decr;
      while(prevChunkIndex > 0 && prevRowIndex < 0) {
        if(prevChunkIndex < arrays.size()) {
          prevRowIndex += arrays[prevChunkIndex]->length();
        } else {
          prevRowIndex += builder->length();
        }
        --prevChunkIndex;
      }
      return IntraColumnIterator(arraysPtr, builder, prevChunkIndex, correctTypesPtr, prevRowIndex);
    }
    void operator++() {
      ++rowIndex;
      if(chunkIndex < arrays.size()) {
        if(rowIndex >= arrays[chunkIndex]->length()) {
          rowIndex = 0;
          ++chunkIndex;
          return;
        }
      }
    }
    void operator--() {
      if(chunkIndex == 0) {
        if(rowIndex == 0) {
          if(chunkIndex < arrays.size()) {
            rowIndex = arrays[chunkIndex]->length() - 1;
          } else {
            rowIndex = builder->length() - 1;
          }
          --chunkIndex;
          return;
        }
      }
      --rowIndex;
    }

    bool isMatchingType() const { return correctTypes[chunkIndex]; }
    void incrementUntilMatchingType() {
      this->operator++();
      if(isMatchingType()) {
        return;
      }
      while(chunkIndex <= arrays.size()) {
        // try next chunk/builder
        ++chunkIndex;
        rowIndex = 0;
        if(isMatchingType()) {
          return;
        }
      }
    }
    void decrementUntilMatchingType() {
      this->operator--();
      if(isMatchingType()) {
        return;
      }
      while(chunkIndex > 0) {
        // try previous chunk/builder
        --chunkIndex;
        rowIndex = arrays[chunkIndex]->length() - 1;
        if(isMatchingType()) {
          return;
        }
      }
    }

  private:
    std::shared_ptr<std::vector<bool>> correctTypesPtr;
    std::shared_ptr<arrow::ArrayVector> arraysPtr;
    std::shared_ptr<arrow::ArrayBuilder> builder;
    size_t chunkIndex;
    size_t rowIndex;

    // cache
    arrow::ArrayVector& arrays;
    std::vector<bool>& correctTypes;
  };

  template <typename T> auto intraColumnBegin(size_t columnIndex) const {
    // retrieve all the array chunks from the child array
    arrow::ArrayVector argChunks;
    argChunks.reserve(numChunks());
    for(size_t chunkIdx = 0; chunkIdx < numChunks(); ++chunkIdx) {
      argChunks.emplace_back(getChildChunk(columnIndex, chunkIdx));
    }
    // + retrieve the child builder if it has been used (and not yet finished into an array)
    std::shared_ptr<arrow::ArrayBuilder> argBuilder = getChildBuilder(columnIndex);
    return IntraColumnIterator<T>(std::move(argChunks), argBuilder);
  }
  template <typename T> auto intraColumnEnd(size_t columnIndex) const {
    // nothing else than chunks size and builder's row indexes matter for the end iterator
    auto builder = getChildBuilder(columnIndex);
    auto builderLength = builder ? builder->length() : 0;
    return IntraColumnIterator<T>(arrow::ArrayVector{}, builder, numChunks(), nullptr,
                                  builderLength);
  }

private:
  bool decomposed;

  MutableChunkedArray arrays;
  std::shared_ptr<ComplexExpressionArrayBuilder> builder;

  CompoundArray const* parentArray = nullptr;
  size_t childIndex = 0;

  CompoundArray const* globalArray = this;
  size_t globalIndex = 0;

  void addArgument(std::string const& argName) {
    arrow::FieldVector fields;
    std::string headName = "List";
    if(builder) {
      headName = builder->getHead().getName();
      auto type = builder->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      fields.insert(fields.end(), structType->fields().begin(), structType->fields().end());
    }
    fields.emplace_back(std::make_shared<arrow::Field>(argName, nullptr));
    builder = std::make_shared<ComplexExpressionArrayBuilder>(Symbol(headName), fields);
  }

  std::shared_ptr<arrow::Field> getField() const {
    if(parentArray != nullptr) {
      return parentArray->getChildField(childIndex);
    }
    return std::make_shared<arrow::Field>("", nullptr);
  }

  std::shared_ptr<arrow::Array> getChildChunk(size_t columnIndex, size_t chunkIndex) const {
    auto& chunkArray = dynamic_cast<ComplexExpressionArray&>(*arrays.chunk(chunkIndex));
    auto argArray = std::dynamic_pointer_cast<ExpressionArray>(chunkArray.field(columnIndex));
    auto const& argArrayData = *chunkArray.data();
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

  std::shared_ptr<ExpressionArrayBuilder> getArgumentBuilder(size_t columnIdx) const {
    if(!builder || builder->num_children() == 0) {
      return nullptr;
    }
    return std::dynamic_pointer_cast<ExpressionArrayBuilder>(builder->child_builder(columnIdx));
  }
};

} // namespace boss::engines::bulk
