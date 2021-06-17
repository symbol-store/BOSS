#pragma once

#include "CompoundArray.hpp"
#include "MutableChunkedArray.hpp"
#include "ValueArrayTypes.hpp"

#include <arrow/array.h>

namespace boss::engines::bulk {

template <typename T> class ValueArrayBase {
private:
  using ArrayType = typename ValueArrayTypeToArrowType<T>::arrayType;
  using BuilderType = typename ValueArrayTypeToArrowType<T>::builderType;

public:
  using ValueType = T;

  explicit ValueArrayBase() : arrays({}), builder(std::make_shared<BuilderType>()) {}

  explicit ValueArrayBase(size_t size) : arrays({}), builder(std::make_shared<BuilderType>()) {
    // TODO: any more efficient way?
    resize(size);
  }

  ValueArrayBase(size_t size, ValueType const& value)
      : arrays({}), builder(std::make_shared<BuilderType>()) {
    // TODO: any efficient way?
    resize(size);
    for(auto& destValue : *this) {
      destValue = value;
    }
  }

  ValueArrayBase(arrow::ArrayVector&& arrays, std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder)
      : arrays(std::move(arrays)),
        builder(std::move(std::dynamic_pointer_cast<BuilderType>(arrayBuilder))) {
    if(!builder) {
      builder = std::make_shared<BuilderType>();
    }
  }

  ValueArrayBase(ValueArrayBase const& other, bool clear = false)
      : arrays({}), builder(std::make_shared<BuilderType>()) {
    if(!clear) {
      // TODO: any efficient way?
      resize(other.size());
      auto otherIt = other.begin();
      for(auto& destValue : *this) {
        destValue = *otherIt;
        ++otherIt;
      }
    }
  }

  ValueArrayBase(ValueArrayBase&& other) noexcept = default;

  ~ValueArrayBase() = default;

  ValueArrayBase& operator=(ValueArrayBase const& other) = delete;
  ValueArrayBase& operator=(ValueArrayBase&& other) = delete;

  // [https://github.com/symbol-store/BOSS/issues/88]
  // need to make this iterate on the arrays as well, not only the builder.
  // it works for now because we use it only for writing new elements and with a
  // workaround: we provide the offset for the elements we cannot iterate
  auto begin() { return builder->begin(arrays.length()); }
  auto end() { return builder->end(); }

  // [https://github.com/symbol-store/BOSS/issues/88] clean up iterators
  // This iterator can iterate on both the arrays and the builder, but read-only
  class ConstIterator {
  public:
    ConstIterator(std::vector<std::shared_ptr<ArrayType>> const& arrays, BuilderType const& builder,
                  size_t chunkIndex, size_t rowIndex = 0)
        : arrays(arrays), builder(builder), chunkIndex(chunkIndex), rowIndex(rowIndex) {}
    ValueType operator*() const {
      // TODO: check how expensive is this condition when using in a loop
      if(chunkIndex >= arrays.size()) {
        return ValueType(*(builder.begin() + rowIndex));
      }
      if constexpr(std::is_same_v<T, Symbol> || std::is_same_v<T, std::string>) {
        return ValueType(std::string(arrays[chunkIndex]->GetView(rowIndex)));
      } else {
        return arrays[chunkIndex]->Value(rowIndex);
      }
    }
    bool operator!=(ConstIterator const& rhs) const {
      return chunkIndex != rhs.chunkIndex || rowIndex != rhs.rowIndex;
    }
    bool operator!=(ConstIterator&& rhs) const {
      return chunkIndex != rhs.chunkIndex || rowIndex != rhs.rowIndex;
    }
    ConstIterator operator+(size_t incr) const {
      size_t nextChunkIndex = chunkIndex;
      size_t nextRowIndex = rowIndex + incr;
      while(nextChunkIndex < arrays.size() && nextRowIndex >= arrays[nextChunkIndex]->length()) {
        nextRowIndex -= arrays[nextRowIndex]->length();
        ++nextChunkIndex;
      }
      return ConstIterator(arrays, builder, nextChunkIndex, nextRowIndex);
    }
    void operator++() {
      ++rowIndex;
      if(chunkIndex < arrays.size()) {
        if(rowIndex >= arrays[chunkIndex]->length()) {
          rowIndex = 0;
          ++chunkIndex;
        }
      }
    }

  private:
    std::vector<std::shared_ptr<ArrayType>> const& arrays;
    BuilderType const& builder;
    size_t chunkIndex;
    size_t rowIndex;
  };

  auto begin() const { return ConstIterator(arrays.getTypedChunks(), *builder, 0); }
  auto end() const {
    return ConstIterator(arrays.getTypedChunks(), *builder, arrays.num_chunks(), builder->length());
  }

  // [https://github.com/symbol-store/BOSS/issues/88]
  /// used to set the owner (parent array) after creating a child array in CompoundArray::column()
  /// so the parent can freezeData() when the child need to freezeData()
  /// since it should always be done together
  void setParent(CompoundArray const* parentArray, size_t childIndex = 0) {
    this->parentArray = parentArray;
    this->childIndex = childIndex;
  }

  /// return arrays + builder in a form that can be used to construct a compound array
  ArrayData data() const {
    std::shared_ptr<arrow::Field> field;
    if(parentArray != nullptr) {
      field = parentArray->getChildField(childIndex);
    } else {
      field = std::make_shared<arrow::Field>("", nullptr);
    }
    return ArrayData(arrays, builder, field);
  }

  /// make sure to have only arrays and not builder left when retrieving the data
  ArrayData freezedData() {
    freezeData();
    return data();
  }

  /// Force the builder to be finished into an array and pushed to the chunkedArray.
  /// It need to be called by any code which can iterate only on arrays
  /// so the data in the builder isn't ignored.
  /// All query operators are able to iterate on the builders, so don't need that.
  /// Currently, it is used only when exporting arrow arrays outside of the backend.
  // [https://github.com/symbol-store/BOSS/issues/88] ideally should be protected
  void freezeData() {
    std::shared_ptr<arrow::Array> chunkArray;
    if(parentArray != nullptr) {
      auto newChunkIndex = arrays.length();
      const_cast<CompoundArray*>(parentArray)->freezeData(); // NOLINT
      if(parentArray->numChunks() <= newChunkIndex) {
        // nothing new
        return;
      }
      // [https://github.com/symbol-store/BOSS/issues/92] handle multiple types in union
      chunkArray = parentArray->getArgument(newChunkIndex, childIndex)->field(0);
    } else {
      if(builder->length() == 0) {
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

  void resize(size_t size) {
    if(size < arrays.length()) {
      // [https://github.com/symbol-store/BOSS/issues/88]
      // any way to handle this case? with slicing?
      // is it a needed case anymore?
      return;
    }
    size -= arrays.length();
    auto builderSize = builder->length();
    if(size > builderSize) {
      auto status = builder->AppendEmptyValues(size - builderSize);
      if(!status.ok()) {
        // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
        return;
      }
    } else if(size < builderSize) {
      // [https://github.com/symbol-store/BOSS/issues/88] need a way to shrink builder size
      // is it a needed case anymore?
      // builder->Resize(size);
    }
  }

  size_t length() const { return builder ? builder->length() + arrays.length() : arrays.length(); }

  void append(Expression const& expression) { append(std::get<ValueType>(expression)); }

  void append(ValueType const& value) {
    auto status = builder->Append(value);
    if(!status.ok()) {
      // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
      return;
    }
  }

protected:
  BuilderType const* getTypedBuilder() const { return builder.get(); }
  std::vector<std::shared_ptr<ArrayType>> const& getTypedArrays() const {
    return arrays.getTypedChunks();
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
        typedChunks.emplace_back(std::move(typedChunk));
      }
    }

    void append(std::shared_ptr<arrow::Array>&& chunk) {
      auto typedChunk = std::dynamic_pointer_cast<ArrayType>(chunk);
      typedChunks.emplace_back(std::move(typedChunk));
      chunks_.emplace_back(std::move(chunk));
      ++length_;
      if(!type_) {
        type_ = chunks_.back()->type();
      }
    }

    std::vector<std::shared_ptr<ArrayType>> const& getTypedChunks() const { return typedChunks; }

    void clear() {
      typedChunks.clear();
      MutableChunkedArray::clear();
    }

  private:
    std::vector<std::shared_ptr<ArrayType>> typedChunks;
  };

  TypedChunkedArray arrays;
  std::shared_ptr<BuilderType> builder;

  CompoundArray const* parentArray = nullptr;
  size_t childIndex = 0;
};

// this is a layer needed only for the bool specialization to work
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
    if(builder != nullptr) {
      bitCount += builder->calculateBitCount();
    }
    return bitCount;
  }
};

} // namespace boss::engines::bulk
