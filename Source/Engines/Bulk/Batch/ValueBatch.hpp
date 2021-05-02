#pragma once

#include "Batch.hpp"

#include "../Utils/CompoundArray.hpp"
#include "../Utils/IterableBuilders.hpp"
#include "../Utils/SymbolArray.hpp"

#include <arrow/array.h>

#include <vector>

namespace boss::engines::bulk {

template <typename T> struct BatchTypeToArrowType;
template <> struct BatchTypeToArrowType<bool> {
  using arrayType = arrow::BooleanArray;
  using builderType = IterableBooleanBuilder;
};
template <> struct BatchTypeToArrowType<int> {
  using arrayType = arrow::Int32Array;
  using builderType = IterableInt32Builder;
};
template <> struct BatchTypeToArrowType<float> {
  using arrayType = arrow::FloatArray;
  using builderType = IterableFloatBuilder;
};
template <> struct BatchTypeToArrowType<std::string> {
  using arrayType = arrow::StringArray;
  using builderType = IterableStringBuilder;
};
template <> struct BatchTypeToArrowType<Symbol> {
  using arrayType = SymbolArray;
  using builderType = SymbolArrayBuilder;
};

template <typename T> class ValueBatch : public Batch {
private:
  using ArrayType = typename BatchTypeToArrowType<T>::arrayType;
  using BuilderType = typename BatchTypeToArrowType<T>::builderType;

public:
  using ValueType = T;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<ValueBatch<T>>();

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ValueType>(); }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ValueType>(val);
  }

  explicit ValueBatch()
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {}
  explicit ValueBatch(std::vector<ValueType> const& values)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>(values)),
        m_builderLogicalSize(values.size()) {}
  explicit ValueBatch(size_t size)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {
    // TODO: any more efficient way?
    resize(size);
  }
  ValueBatch(size_t size, ValueType const& value)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {
    // TODO: any efficient way?
    resize(size);
    for(auto& batchValue : *this) {
      batchValue = value;
    }
  }
  ValueBatch(size_t size, ValueType&& value)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {
    // TODO: any efficient way?
    resize(size);
    for(auto& batchValue : *this) {
      batchValue = value;
    }
  }

  ValueBatch(arrow::ArrayVector&& arrays, std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder)
      : m_arrays(std::move(arrays)),
        m_builder(std::move(std::dynamic_pointer_cast<BuilderType>(arrayBuilder))),
        m_builderLogicalSize(m_builder ? m_builder->length() : 0) {
    if(!m_builder) {
      m_builder = std::make_shared<BuilderType>();
    }
  }

  ValueBatch(ValueBatch const& other, bool clear = false)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {
    if(!clear) {
      // TODO: any efficient way?
      resize(other.size());
      auto otherIt = other.begin();
      for(auto& batchValue : *this) {
        batchValue = *otherIt;
        ++otherIt;
      }
    }
  }

  ValueBatch(ValueBatch&& other, bool clear = false) noexcept
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {
    if(!clear) {
      // TODO: any efficient way?
      resize(other.size());
      auto otherIt = other.begin();
      for(auto& batchValue : *this) {
        batchValue = *otherIt;
        ++otherIt;
      }
    }
  }

  ~ValueBatch() override = default;
  ValueBatch& operator=(ValueBatch const& other) = delete;
  ValueBatch& operator=(ValueBatch&& other) = delete;

  WritablePtr clone(bool clear = false) const override {
    return WritablePtr(cloneAsValueBatch(clear));
  }
  virtual WritableBatchPtr<ValueBatch> cloneAsValueBatch(bool clear = false) const {
    return WritableBatchPtr(new ValueBatch(*this, clear));
  }

  template <typename BatchType, std::enable_if_t<std::is_base_of_v<BatchType, ValueBatch>, int> = 0>
  WritableBatchPtr<BatchType> cloneAs(bool clear = false) const {
    return cloneAsValueBatch(clear);
  }

  void clear() override {
    m_arrays.clear();
    m_builder->Reset();
    m_builderLogicalSize = 0;
  }

  // TODO: need to make this iterate on the arrays as well, not only the builder
  // it works for now because we use it only for writing new elements
  auto begin() { return m_builder->begin(); }
  auto end() { return m_builder->begin() + m_builderLogicalSize; }

  class ConstIterator {
  public:
    using value_type = ValueType;
    ConstIterator(std::vector<std::shared_ptr<ArrayType>> const& arrays, BuilderType const& builder,
                  size_t chunkIndex, size_t rowIndex = 0)
        : m_arrays(arrays), m_builder(builder), m_chunkIndex(chunkIndex), m_rowIndex(rowIndex),
          m_constant(arrays.size() <= 1 &&
                     (arrays.empty() ? builder.length() : builder.length() + arrays[0]->length()) ==
                         1) {}
    ValueType operator*() const {
      // TODO: check how expensive is this check in the loop
      // need to clean up this code...
      if(m_chunkIndex >= m_arrays.size()) {
        return m_constant ? ValueType(*m_builder.begin())
                          : ValueType(*(m_builder.begin() + m_rowIndex));
      }
      // TODO: check if loop invariant optimization takes care of the m_constant check
      if constexpr(std::is_same_v<T, Symbol>) {
        return m_constant ? ValueType(std::string(m_arrays[0]->GetView(0)))
                          : ValueType(std::string(m_arrays[m_chunkIndex]->GetView(m_rowIndex)));
      } else if constexpr(std::is_same_v<T, std::string>) {
        return m_constant ? ValueType(m_arrays[0]->GetView(0))
                          : ValueType(m_arrays[m_chunkIndex]->GetView(m_rowIndex));
      } else {
        return m_constant ? m_arrays[0]->Value(0) : m_arrays[m_chunkIndex]->Value(m_rowIndex);
      }
    }
    bool operator!=(ConstIterator const& rhs) const {
      return m_chunkIndex != rhs.m_chunkIndex || m_rowIndex != rhs.m_rowIndex;
    }
    bool operator!=(ConstIterator&& rhs) const {
      return m_chunkIndex != rhs.m_chunkIndex || m_rowIndex != rhs.m_rowIndex;
    }
    ConstIterator operator+(size_t incr) const {
      size_t chunkIndex = m_chunkIndex;
      size_t rowIndex = m_rowIndex + incr;
      while(chunkIndex < m_arrays.size() && rowIndex >= m_arrays[chunkIndex]->length()) {
        rowIndex -= m_arrays[chunkIndex]->length();
        ++chunkIndex;
      }
      return ConstIterator(m_arrays, m_builder, chunkIndex, rowIndex);
    }
    void operator++() {
      // TODO: check if loop invariant optimization takes care of the m_constant check
      if(m_constant) {
        m_rowIndex = m_builder.length();
        m_chunkIndex = m_arrays.size();
        return;
      }
      ++m_rowIndex;
      if(m_chunkIndex < m_arrays.size()) {
        if(m_rowIndex >= m_arrays[m_chunkIndex]->length()) {
          m_rowIndex = 0;
          ++m_chunkIndex;
        }
      }
    }

  private:
    std::vector<std::shared_ptr<ArrayType>> const& m_arrays;
    BuilderType const& m_builder;
    size_t m_chunkIndex;
    size_t m_rowIndex;
    bool m_constant; // allow single element arrays to safely be used in iterations
  };

  auto begin() const { return ConstIterator(m_arrays.typedChunks(), *m_builder, 0); }

  auto end() const {
    return ConstIterator(m_arrays.typedChunks(), *m_builder, m_arrays.num_chunks(),
                         m_builder->length());
  }

  void setOwner(std::shared_ptr<CompoundArray> parentArray, size_t childIndex) override {
    m_parentArray = std::move(parentArray);
    m_childIndex = childIndex;
  }

  void freezeData() {
    std::shared_ptr<arrow::Array> chunkArray;
    if(m_parentArray) {
      auto newChunkIndex = m_arrays.length();
      m_parentArray->freezeData();
      if(m_parentArray->numChunks() <= newChunkIndex) {
        // nothing new
        return;
      }
      // TODO: handle multiple types in union
      chunkArray = m_parentArray->getArgument(newChunkIndex, m_childIndex)->field(0);
    } else {
      if(m_builder->length() == 0) {
        // nothing new
        return;
      }
      auto result = m_builder->Finish(&chunkArray);
      if(!result.ok()) {
        // failed
        return;
      }
    }

    if(m_builderLogicalSize < chunkArray->length()) {
      m_arrays.append(chunkArray->Slice(0, m_builderLogicalSize));
    } else {
      m_arrays.append(std::move(chunkArray));
    }
    m_builderLogicalSize = 0;
  }

  void reserve(size_t size) override {
    if(size <= m_arrays.length()) {
      return;
    }
    size -= m_arrays.length();
    if(size > m_builder->capacity()) {
      auto status = m_builder->Reserve(size - m_builder->length());
      if(!status.ok()) {
        return;
      }
    }
  }

  void resize(size_t size) override {
    if(size < m_arrays.length()) {
      // TODO: any way to handle this case? with slicing?
      return;
    }
    size -= m_arrays.length();
    if(size > m_builderLogicalSize) {
      if constexpr(std::is_same_v<T, std::string> || std::is_same_v<T, Symbol>) {
        // TODO: cleaner implementation
        // don't resize the internal data in advance when using the proxy
        // since it cannot revisit previous empty values
        // m_builder.Reserve(size - m_builder.length()); // and cannot reserve neither! since append
        // will take care of that
      } else {
        auto status = m_builder->AppendEmptyValues(size - m_builder->length());
        if(!status.ok()) {
          return;
        }
      }
    } else if(size < m_builderLogicalSize) {
      // TODO: need a way to shrink builder size
      // m_builder->Resize(size);
    }
    m_builderLogicalSize = size;
  }

  size_t size() const override { return m_builderLogicalSize + m_arrays.length(); }

  void insert(Expression const& expression) override { insert(std::get<ValueType>(expression)); }

  void insert(ValueType const& value) {
    auto status = m_builder->Append(value);
    if(!status.ok()) {
      return;
    }
    ++m_builderLogicalSize;
  }

  BatchData data() const override { return BatchData(m_arrays, m_builder, m_builderLogicalSize); }

  bool evaluate(ReadablePtr& outputPtr) const override {
    outputPtr.reset();
    return false;
  }

private:
  class MutableChunkedArray : public arrow::ChunkedArray {
  public:
    explicit MutableChunkedArray(arrow::ArrayVector&& arrays)
        : arrow::ChunkedArray(std::move(arrays), nullptr) {
      for(auto& chunk : chunks()) {
        m_typedChunks.emplace_back(std::dynamic_pointer_cast<ArrayType>(chunk));
      }
    }

    std::vector<std::shared_ptr<ArrayType>> const& typedChunks() const { return m_typedChunks; }

    void reserve(size_t chunkSize) { chunks_.reserve(chunkSize); }

    void append(std::shared_ptr<arrow::Array> chunkArray) {
      length_ += chunkArray->length();
      null_count_ += chunkArray->null_count();
      m_typedChunks.emplace_back(std::dynamic_pointer_cast<ArrayType>(chunkArray));
      chunks_.emplace_back(std::move(chunkArray));

      if(!type_ && !chunks_.empty()) {
        type_ = chunks_[0]->type();
      }
    }

    void clear() {
      m_typedChunks.clear();
      chunks_.clear();
      length_ = 0;
      null_count_ = 0;
      type_.reset();
    }

  private:
    std::vector<std::shared_ptr<ArrayType>> m_typedChunks;
  };

  MutableChunkedArray m_arrays;
  std::shared_ptr<BuilderType> m_builder;
  size_t m_builderLogicalSize; // needed until we can shrink a builder
  std::shared_ptr<CompoundArray> m_parentArray;
  size_t m_childIndex = 0;
};

} // namespace boss::engines::bulk
