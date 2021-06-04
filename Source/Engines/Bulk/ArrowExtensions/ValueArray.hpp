#pragma once

#include "../ArrowExtensions/CompoundArray.hpp"
#include "../ArrowExtensions/IterableBuilders.hpp"
#include "../ArrowExtensions/MutableChunkedArray.hpp"
#include "../ArrowExtensions/SymbolArray.hpp"

#include <arrow/array.h>

namespace boss::engines::bulk {

// builder type and array type we use for all the data types supported by ValueArray
template <typename T> struct ValueArrayTypeToArrowType;
template <> struct ValueArrayTypeToArrowType<bool> {
  using arrayType = arrow::BooleanArray;
  using builderType = IterableBooleanBuilder;
};
template <> struct ValueArrayTypeToArrowType<int> {
  using arrayType = arrow::Int32Array;
  using builderType = IterableInt32Builder;
};
template <> struct ValueArrayTypeToArrowType<float> {
  using arrayType = arrow::FloatArray;
  using builderType = IterableFloatBuilder;
};
template <> struct ValueArrayTypeToArrowType<std::string> {
  using arrayType = arrow::StringArray;
  using builderType = IterableStringBuilder<>;
};
template <> struct ValueArrayTypeToArrowType<Symbol> {
  using arrayType = SymbolArray;
  using builderType = SymbolArrayBuilder;
};

template <typename T> class ValueArrayBase {
private:
  using ArrayType = typename ValueArrayTypeToArrowType<T>::arrayType;
  using BuilderType = typename ValueArrayTypeToArrowType<T>::builderType;

public:
  using ValueType = T;

  explicit ValueArrayBase()
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {}

  explicit ValueArrayBase(std::vector<ValueType> const& values)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>(values)),
        m_builderLogicalSize(values.size()) {}

  explicit ValueArrayBase(size_t size)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {
    // TODO: any more efficient way?
    resize(size);
  }

  ValueArrayBase(size_t size, ValueType const& value)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {
    // TODO: any efficient way?
    resize(size);
    for(auto& batchValue : *this) {
      batchValue = value;
    }
  }

  ValueArrayBase(size_t size, ValueType&& value)
      : m_arrays({}), m_builder(std::make_shared<BuilderType>()), m_builderLogicalSize(0) {
    // TODO: any efficient way?
    resize(size);
    for(auto& batchValue : *this) {
      batchValue = value;
    }
  }

  ValueArrayBase(arrow::ArrayVector&& arrays, std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder)
      : m_arrays(std::move(arrays)),
        m_builder(std::move(std::dynamic_pointer_cast<BuilderType>(arrayBuilder))),
        m_builderLogicalSize(m_builder ? m_builder->length() : 0) {
    if(!m_builder) {
      m_builder = std::make_shared<BuilderType>();
    }
  }

  ValueArrayBase(ValueArrayBase const& other, bool clear = false)
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

  ValueArrayBase(ValueArrayBase&& other, bool clear = false) noexcept
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

  ~ValueArrayBase() = default; /*{
    // if the logical size of the builder doesn't match the stored size
    // we need to finish the builder into an array!
    // otherwise now that we destroy this representation, the logical size will be lost
    if(m_builder && m_builder->length() != m_builderLogicalSize) {
      freezeData();
    }
  }*/

  ValueArrayBase& operator=(ValueArrayBase const& other) = delete;
  ValueArrayBase& operator=(ValueArrayBase&& other) = delete;

  // [https://github.com/symbol-store/BOSS/issues/88]
  // need to make this iterate on the arrays as well, not only the builder.
  // it works for now because we use it only for writing new elements and with this
  // workaround: we provide the offset for the elements we cannot iterate
  auto begin() { return m_builder->begin(m_arrays.length()); }
  auto end() { return m_builder->begin(m_arrays.length()) + m_builderLogicalSize; }

  // [https://github.com/symbol-store/BOSS/issues/88] clean up iterators
  class ConstIterator {
  public:
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

  // [https://github.com/symbol-store/BOSS/issues/88]
  void setOwner(CompoundArray const* parentArray, size_t childIndex = 0) {
    // used to set the owner (parent batch) after creating a child batch in CompoundBatch::column()
    // so the parent can freezeData() when the child need to freezeData()
    // since it should always be done together
    m_parentArray = parentArray;
    m_childIndex = childIndex;
  }

  BatchData data() const {
    std::shared_ptr<arrow::Field> field;
    if(m_parentArray != nullptr) {
      field = m_parentArray->childField(m_childIndex);
    } else {
      field = std::make_shared<arrow::Field>("", nullptr);
    }
    return BatchData(m_arrays, m_builder, m_builderLogicalSize, field);
  }

  BatchData freezedData() {
    freezeData();
    return data();
  }

  /// Force the builder to be finished into an array and pushed to the chunkedArray.
  /// It need to be called by any code which can iterate only on arrays
  /// so the data in the builder isn't ignored.
  /// All query operators are able to iterate on the builders, so don't need that.
  /// Currently, it is used only when exporting arrow arrays outside of the backend.
  void freezeData() {
    std::shared_ptr<arrow::Array> chunkArray;
    if(m_parentArray) {
      auto newChunkIndex = m_arrays.length();
      const_cast<CompoundArray*>(m_parentArray)->freezeData(); // NOLINT
      if(m_parentArray->numChunks() <= newChunkIndex) {
        // nothing new
        return;
      }
      // [https://github.com/symbol-store/BOSS/issues/92] handle multiple types in union
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

  void resize(size_t size) {
    if(size < m_arrays.length()) {
      // [https://github.com/symbol-store/BOSS/issues/88]
      // any way to handle this case? with slicing?
      return;
    }
    size -= m_arrays.length();
    if(size > m_builderLogicalSize) {
      if constexpr(std::is_same_v<T, std::string> || std::is_same_v<T, Symbol>) {
        // [https://github.com/symbol-store/BOSS/issues/88] need cleaner implementation
        // don't resize the internal data in advance when using the proxy
        // since it cannot revisit previous empty values
        // since append will take care of that
      } else {
        auto status = m_builder->AppendEmptyValues(size - m_builder->length());
        if(!status.ok()) {
          return;
        }
      }
    } else if(size < m_builderLogicalSize) {
      // [https://github.com/symbol-store/BOSS/issues/88] need a way to shrink builder size
      // is it a needed case anymore?
      // m_builder->Resize(size);
    }
    m_builderLogicalSize = size;
  }

  size_t length() const { return m_builderLogicalSize + m_arrays.length(); }

  void append(Expression const& expression) { append(std::get<ValueType>(expression)); }

  void append(ValueType const& value) {
    auto status = m_builder->Append(value);
    if(!status.ok()) {
      return;
    }
    ++m_builderLogicalSize;
  }

protected:
  BuilderType const* getTypedBuilder() const { return m_builder.get(); }
  std::vector<std::shared_ptr<ArrayType>> const& getTypedArrays() const {
    return m_arrays.typedChunks();
  }

private:
  /// This class is additionally storing a vector of the cast array types
  /// to avoid having to do the cast at every iteration (see iterators)
  class TypedChunkedArray : public MutableChunkedArray {
  public:
    explicit TypedChunkedArray(arrow::ArrayVector&& arrays)
        : MutableChunkedArray(std::move(arrays)) {
      for(auto& chunk : chunks()) {
        auto typedChunk = std::dynamic_pointer_cast<ArrayType>(chunk);
        m_typedChunks.emplace_back(std::move(typedChunk));
      }
    }

    void append(std::shared_ptr<arrow::Array>&& chunk) {
      auto typedChunk = std::dynamic_pointer_cast<ArrayType>(chunk);
      m_typedChunks.emplace_back(std::move(typedChunk));
      chunks_.emplace_back(std::move(chunk));
      ++length_;
      if(!type_) {
        type_ = chunks_.back()->type();
      }
    }

    std::vector<std::shared_ptr<ArrayType>> const& typedChunks() const { return m_typedChunks; }

    void clear() {
      m_typedChunks.clear();
      MutableChunkedArray::clear();
    }

  private:
    std::vector<std::shared_ptr<ArrayType>> m_typedChunks;
  };

  TypedChunkedArray m_arrays;
  std::shared_ptr<BuilderType> m_builder;

  // [https://github.com/symbol-store/BOSS/issues/88]
  // needed until we can shrink a builder
  size_t m_builderLogicalSize;

  CompoundArray const* m_parentArray = nullptr;
  size_t m_childIndex = 0;
};

template <typename T> class ValueArray : public ValueArrayBase<T> {
private:
  using ValueArrayBase = ValueArrayBase<T>;
public:
  using ValueArrayBase::ValueArrayBase;
};

// Specialization for bool arrays, to add functions on the bit vector
template <> class ValueArray<bool> : public ValueArrayBase<bool> {
private:
  using ValueArrayBase = ValueArrayBase<bool>;
public:
  using ValueArrayBase::ValueArrayBase;

  size_t calculateBitCount() const {
    size_t bitCount = 0;
    auto const& arrays = ValueArrayBase::getTypedArrays();
    for(auto const& arrayPtr : arrays) {
      bitCount += arrayPtr->true_count();
    }
    auto const* builder = ValueArrayBase::getTypedBuilder();
    if(builder) {
      bitCount += builder->calculateBitCount();
    }
    return bitCount;
  }
};

} // namespace boss::engines::bulk
