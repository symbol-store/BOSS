#pragma once

#include "BatchData.hpp"
#include "ComplexExpressionArray.hpp"
#include "ExpressionArray.hpp"

#include "../../../Expression.hpp"

#include <arrow/chunked_array.h>

#include <iostream>
#include <string>
#include <vector>

namespace boss::engines::bulk {

class CompoundArray : private arrow::ChunkedArray {
  // can we use delegation here? I feel that private inheritence is (often) a code smell
  // do you think we should make the arrow implementation a template parameter?
public:
  explicit CompoundArray(Symbol const& head, std::vector<std::string> const& columns)
      : arrow::ChunkedArray(arrow::ArrayVector{}, nullptr),
        m_builder(std::make_shared<ComplexExpressionArrayBuilder>(head, columns)) {}

  explicit CompoundArray(Symbol const& head, size_t argCount)
      : arrow::ChunkedArray(arrow::ArrayVector{}, nullptr),
        m_builder(std::make_shared<ComplexExpressionArrayBuilder>(head, argCount)) {}

  CompoundArray() : arrow::ChunkedArray(arrow::ArrayVector{}, nullptr), m_builder(nullptr) {}

  CompoundArray(arrow::ArrayVector&& argArrays, std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder)
      : arrow::ChunkedArray(std::move(argArrays), nullptr),
        m_builder(
            std::move(std::dynamic_pointer_cast<ComplexExpressionArrayBuilder>(arrayBuilder))) {
    if(!chunks_.empty()) {
      type_ = chunks_[0]->type();
    }
  }

  CompoundArray(CompoundArray&& other) noexcept
      : arrow::ChunkedArray(std::move(other.chunks_), std::move(other.type_)),
        m_builder(std::move(other.m_builder)) {}

  CompoundArray(CompoundArray const& other, bool clear = false)
      : arrow::ChunkedArray(arrow::ArrayVector{}, nullptr), m_builder(nullptr) {
    if(!clear) {
      auto const otherChunks = other.getChunkedArray().chunks();
      chunks_.insert(chunks_.begin(), otherChunks.begin(), otherChunks.end());
      type_ = other.type_;
    }
  }

  CompoundArray(CompoundArray const& other, std::shared_ptr<arrow::Array> const& singleArray)
      : arrow::ChunkedArray(singleArray),
        m_builder(other.m_builder
                      ? std::make_shared<ComplexExpressionArrayBuilder>(*other.m_builder, true)
                      : nullptr) {}

  CompoundArray(CompoundArray const& other, std::shared_ptr<arrow::Array>&& singleArray)
      : arrow::ChunkedArray(std::move(singleArray)),
        m_builder(other.m_builder
                      ? std::make_shared<ComplexExpressionArrayBuilder>(*other.m_builder, true)
                      : nullptr) {}

  ~CompoundArray() = default;
  CompoundArray& operator=(CompoundArray const& other) = delete;
  CompoundArray& operator=(CompoundArray&& other) = delete;

  arrow::ChunkedArray const& getChunkedArray() const { return *this; }

  arrow::Status append(ComplexExpression const& expression) {
    if(!m_builder) {
      // initialise builder with the right arguments count at the first insert
      // if it hasn't been specified and initialised yet
      m_builder = std::make_shared<ComplexExpressionArrayBuilder>(expression.getHead(),
                                                                  expression.getArguments().size());
    }

    // TODO: need to implement later one of these methods:
    // A) supporting multiple builders to avoid splitting data too much
    // B) append all to the same builder and handle well union array with multiple fields
    // but for now, make sure to split into new chunks every time there is a new type of expression
    if(m_builder->num_fields() > 0) {
      if(!m_builder->IsSupported(expression)) {
        freezeData();
      }
    }

    return m_builder->AppendExpression(expression);
  }

  arrow::Status append(Symbol const& head, std::vector<BatchData> const& argData) {
    if(!m_builder) {
      m_builder = std::make_shared<ComplexExpressionArrayBuilder>(head, argData.size());
    }

    return m_builder->AppendExpressions(argData);
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

  void merge(CompoundArray&& other) {
    chunks_.insert(chunks_.end(), other.chunks_.begin(), other.chunks_.end());
    length_ += other.length_;
    null_count_ += other.null_count_;

    if(other.m_builder && m_builder) {
      // need to finalise first the existing builder
      freezeData();
    }

    m_builder = std::move(other.m_builder);

    if(!type_) {
      type_ = other.type_;
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
      auto status = m_builder->resizeStructArray(size);
      if(!status.ok()) {
        return;
      }
    }
  }

  void clear() {
    m_builder.reset();
    chunks_.clear();
    length_ = 0;
    null_count_ = 0;
    type_.reset();
  }

  void setOwner(std::shared_ptr<CompoundArray> parentArray, size_t childIndex) {
    m_parentArray = std::move(parentArray);
    m_childIndex = childIndex;
  }

  void freezeData() {
    std::shared_ptr<arrow::Array> chunkArray;
    if(m_parentArray) {
      auto newChunkIndex = chunks_.size();
      m_parentArray->freezeData();
      if(m_parentArray->numChunks() <= newChunkIndex) {
        // nothing new
        return;
      }
      // TODO: handle multiple types in union
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

    length_ += chunkArray->length();
    null_count_ += chunkArray->null_count();
    chunks_.emplace_back(std::move(chunkArray));

    if(!type_ && !chunks_.empty()) {
      type_ = chunks_[0]->type();
    }
  }

  size_t length() const {
    if(m_builder) {
      return arrow::ChunkedArray::length() + m_builder->length();
    }
    return arrow::ChunkedArray::length();
  }

  size_t numChunks() const { return arrow::ChunkedArray::num_chunks(); }
  // should these be private?

  size_t numArguments() const {
    if(num_chunks() > 0) {
      return chunk(0)->num_fields();
    }
    return m_builder ? m_builder->num_fields() : 0;
  }

  std::shared_ptr<arrow::ArrayBuilder> getBuilder() const { return m_builder; }

  std::shared_ptr<ExpressionArrayBuilder> getArgumentBuilder(size_t columnIdx) const {
    // should this be private?
    if(!m_builder || m_builder->num_children() == 0) {
      return nullptr;
    }
    return std::dynamic_pointer_cast<ExpressionArrayBuilder>(m_builder->child_builder(columnIdx));
  }

  std::shared_ptr<ExpressionArray> getArgument(size_t chunkIdx, size_t columnIdx) const {
    auto& chunkArray = dynamic_cast<ComplexExpressionArray&>(*chunk(chunkIdx));
    return std::dynamic_pointer_cast<ExpressionArray>(chunkArray.field(columnIdx));
  }

  std::shared_ptr<arrow::Array> getRow(size_t rowIdx) const {
    if(rowIdx >= arrow::ChunkedArray::length()) {
      // extracting row from the builder
      // need to finish it first...
      const_cast<CompoundArray*>(this)->freezeData(); // NOLINT
    }
    return Slice(rowIdx, 1)->chunk(0);
  }

private:
  using ComplexExpressionArrayBuilder = ComplexExpressionArrayBuilder<ExpressionArrayBuilder>;

  std::shared_ptr<ComplexExpressionArrayBuilder> m_builder;

  std::shared_ptr<CompoundArray> m_parentArray = nullptr;
  size_t m_childIndex = 0;
};

} // namespace boss::engines::bulk
