#pragma once

#include "../BatchData.hpp"
#include "ComplexExpressionArray.hpp"
#include "ExpressionArray.hpp"

#include "../ArrowExtensions/MutableChunkedArray.hpp"

#include "../../../Expression.hpp"

#include <string>
#include <vector>

namespace boss::engines::bulk {

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

  explicit CompoundArray(Symbol const& head, size_t argCount)
      : m_builder(std::make_shared<ComplexExpressionArrayBuilder>(head, argCount)) {}

  CompoundArray() : m_builder(nullptr) {}

  CompoundArray(arrow::ArrayVector&& argArrays, std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder)
      : m_arrays(std::move(argArrays)),
        m_builder(
            std::move(std::dynamic_pointer_cast<ComplexExpressionArrayBuilder>(arrayBuilder))) {}

  CompoundArray(CompoundArray&& other) noexcept
      : m_arrays(std::move(other.m_arrays)), m_builder(std::move(other.m_builder)) {}

  CompoundArray(CompoundArray const& other, bool clear = false)
      : m_builder(other.m_builder != nullptr
                      ? std::make_shared<ComplexExpressionArrayBuilder>(*other.m_builder, clear)
                      : nullptr) {
    if(!clear) {
      m_arrays.append(other.m_arrays);
    }
  }

  CompoundArray(CompoundArray const& other, std::shared_ptr<arrow::Array> const& singleArray)
      : m_arrays({singleArray}),
        m_builder(other.m_builder != nullptr
                      ? std::make_shared<ComplexExpressionArrayBuilder>(*other.m_builder, true)
                      : nullptr) {}

  CompoundArray(CompoundArray const& other, std::shared_ptr<arrow::Array>&& singleArray)
      : m_arrays({std::move(singleArray)}),
        m_builder(other.m_builder != nullptr
                      ? std::make_shared<ComplexExpressionArrayBuilder>(*other.m_builder, true)
                      : nullptr) {}

  explicit CompoundArray(std::shared_ptr<ComplexExpressionArrayBuilder> const& builder)
      : m_builder(std::make_shared<ComplexExpressionArrayBuilder>(*builder, false)) {}

  explicit CompoundArray(std::shared_ptr<ComplexExpressionArrayBuilder>&& builder)
      : m_builder(std::make_shared<ComplexExpressionArrayBuilder>(*builder, false)) {}

  ~CompoundArray() = default;
  CompoundArray& operator=(CompoundArray const& other) = delete;
  CompoundArray& operator=(CompoundArray&& other) = delete;

  arrow::ChunkedArray const& getChunkedArray() const { return m_arrays; }

  arrow::Status append(ComplexExpression const& expression) {
    // [https://github.com/symbol-store/BOSS/issues/92] implement later one of these methods:
    // A) supporting multiple builders to avoid splitting data too much
    // B) append all to the same builder and handle well union array with multiple fields
    // But for now, make sure to split into new chunks every time there is a new type of expression
    if(m_builder && m_builder->num_fields() > 0) {
      if(!m_builder->IsSupported(expression)) {
        freezeData();
        m_builder = nullptr;
      }
    }

    if(!m_builder) {
      // initialise builder with the right arguments count at the first insert
      // if it hasn't been specified and initialised yet
      m_builder = std::make_shared<ComplexExpressionArrayBuilder>(expression.getHead(),
                                                                  expression.getArguments().size());
    }

    return m_builder->AppendExpression(expression);
  }

  arrow::Status append(Symbol const& head, std::vector<BatchData> const& argData) {
    // [https://github.com/symbol-store/BOSS/issues/92] implement later one of these methods:
    // A) supporting multiple builders to avoid splitting data too much
    // B) append all to the same builder and handle well union array with multiple fields
    // But for now, make sure to split into new chunks every time there is a new type of expression
    if(m_builder && !m_builder->IsSupported(argData)) {
      freezeData();
      // try to copy the fields (to transfer column names if it is set)
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

    return m_builder->AppendExpressions(argData);
  }

  void append(CompoundArray&& other) {
    m_arrays.append(std::move(other.m_arrays));

    if(other.m_builder && m_builder) {
      // need to finalise first the existing builder
      freezeData();
    }

    m_builder = std::move(other.m_builder);
  }

  arrow::Status initArguments(Symbol const& head, std::vector<BatchData> const& argData) {
    if(!m_builder) {
      m_builder = std::make_shared<ComplexExpressionArrayBuilder>(head, argData.size());
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

    return m_builder->initArguments(types);
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
      auto status = m_builder->resizeStructArray(size);
      if(!status.ok()) {
        return;
      }
    }
  }

  void clear() {
    m_arrays.clear();
    m_builder.reset();
  }

  void setOwner(std::shared_ptr<CompoundArray> parentArray, size_t childIndex) {
    m_parentArray = std::move(parentArray);
    m_childIndex = childIndex;
  }

  void freezeData() {
    std::shared_ptr<arrow::Array> chunkArray;
    if(m_parentArray) {
      auto newChunkIndex = m_arrays.chunks().size();
      m_parentArray->freezeData();
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

  std::shared_ptr<arrow::Field> field() {
    if(m_parentArray) {
      return m_parentArray->childField(m_childIndex);
    }
    return std::make_shared<arrow::Field>("", nullptr);
  }

  std::shared_ptr<arrow::Field> childField(size_t index) {
    auto fields = childFields();
    if(!fields.empty()) {
      return fields[index];
    }
    return std::make_shared<arrow::Field>("", nullptr);
  }

  size_t length() const {
    if(m_builder) {
      return m_arrays.length() + m_builder->length();
    }
    return m_arrays.length();
  }

  // [https://github.com/symbol-store/BOSS/issues/88] ideally should not be exposed
  size_t numChunks() const { return m_arrays.num_chunks(); }

  size_t numArguments() const {
    if(m_arrays.num_chunks() > 0) {
      return m_arrays.chunk(0)->num_fields();
    }
    return m_builder && m_builder->length() > 0 ? m_builder->num_fields() : 0;
  }

  bool hasBuilder() const { return bool(m_builder && m_builder->length() > 0); }
  std::shared_ptr<ComplexExpressionArrayBuilder> getBuilder() const { return m_builder; }

  void addArgument(Symbol const& head, std::string const& argName) {
    arrow::FieldVector fields;
    if(m_builder) {
      auto type = m_builder->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      fields.insert(fields.begin(), structType->fields().begin(), structType->fields().end());
    }
    fields.emplace_back(std::make_shared<arrow::Field>(argName, nullptr));
    m_builder = std::make_shared<ComplexExpressionArrayBuilder>(head, fields);
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

private:
  MutableChunkedArray m_arrays;
  std::shared_ptr<ComplexExpressionArrayBuilder> m_builder;

  std::shared_ptr<CompoundArray> m_parentArray = nullptr;
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
};

} // namespace boss::engines::bulk
