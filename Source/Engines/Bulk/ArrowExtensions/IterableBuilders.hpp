#pragma once

#include <arrow/array/builder_binary.h>
#include <arrow/array/builder_primitive.h>
#include <arrow/util/bit_util.h>
#include <arrow/util/bitmap_ops.h>

namespace boss::engines::bulk {

/** Special case to iterate a boolean builder.
 * This is because data is compressed into a bit array,
 * so we cannot read and write data the same as we do for other simple data types.
 * It has to be converted in and out using arrow::bitUtil functions. */
class IterableBooleanBuilder : public arrow::BooleanBuilder {
public:
  explicit IterableBooleanBuilder(arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::BooleanBuilder(pool) {}

  int64_t calculateBitCount() const {
    return arrow::internal::CountSetBits(data_builder_.data(), 0, data_builder_.length());
  }

  bool GetValue(int64_t index) const { return arrow::BitUtil::GetBit(data_builder_.data(), index); }

  void SetValue(int64_t index, bool value) {
    arrow::BitUtil::SetBitTo(data_builder_.mutable_data(), index, value);
  }

  bool operator[](int64_t index) const { return GetValue(index); }

  uint8_t const* raw_values() const { return data_builder_.data(); }
  uint8_t* raw_values() { return data_builder_.mutable_data(); }

  // need a proxy to pretend having a reference to a bool
  // when we actually need to go through the SetValue/GetValue interface of the array
  class BoolBuilderProxy {
  public:
    BoolBuilderProxy(IterableBooleanBuilder& builder, int index) : builder(builder), index(index) {}
    explicit operator bool() const { return builder.GetValue(index); }
    BoolBuilderProxy& operator=(bool value) {
      builder.SetValue(index, value);
      return *this;
    }
    IterableBooleanBuilder& getBuilder() const { return builder; }
    int getIndex() const { return index; }
    void incrementIndex() { ++index; }

  private:
    IterableBooleanBuilder& builder;
    int index;
  };

  class BoolIterator {
  public:
    BoolIterator(IterableBooleanBuilder& builder, int index) : proxy(builder, index) {}
    auto& operator*() { return proxy; }
    bool operator!=(BoolIterator const& rhs) const {
      return proxy.getIndex() != rhs.proxy.getIndex();
    }
    bool operator!=(BoolIterator&& rhs) const { return proxy.getIndex() != rhs.proxy.getIndex(); }
    BoolIterator operator+(int incr) const {
      return BoolIterator(proxy.getBuilder(), proxy.getIndex() + incr);
    }
    void operator++() { proxy.incrementIndex(); }

  private:
    BoolBuilderProxy proxy;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(int offset = 0) { return BoolIterator(*this, -offset); }
  auto end() { return BoolIterator(*this, length()); }

  class BoolConstIterator {
  public:
    BoolConstIterator(IterableBooleanBuilder const& builder, int index)
        : builder(builder), index(index) {}
    auto operator*() const { return builder[index]; }
    bool operator!=(BoolConstIterator const& rhs) const { return index != rhs.index; }
    bool operator!=(BoolConstIterator&& rhs) const { return index != rhs.index; }
    BoolConstIterator operator+(int incr) const { return BoolConstIterator(builder, index + incr); }
    void operator++() { ++index; }

  private:
    IterableBooleanBuilder const& builder;
    int index;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(int offset = 0) const { return BoolConstIterator(*this, -offset); }
  auto end() const { return BoolConstIterator(*this, length()); }
};

/** Special case to iterate a string builder.
 * When we reserve, we only adjust the offset array
 * Then, when use a proxy to do the actual Append when we set a string value.
 * This is because, unlike other builders, we don't know in advance the size we need to allocate.
 */
template <typename ElementType = std::string> // allow to handle Symbol as well
class IterableStringBuilder : public arrow::StringBuilder {
  // [https://github.com/symbol-store/BOSS/issues/88] we could implement it in a better way
  // for example, using a dictionary for strings, so can separate ordered insert and data allocation
private:
  size_t actualSize;

public:
  explicit IterableStringBuilder(arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StringBuilder(pool), actualSize(0) {}

  arrow::Status Append(const uint8_t* value, offset_type length) {
    ++actualSize;
    return arrow::StringBuilder::Append(value, length);
  }

  arrow::Status Append(const char* value, offset_type length) {
    ++actualSize;
    return arrow::StringBuilder::Append(value, length);
  }

  arrow::Status Append(arrow::util::string_view value) {
    ++actualSize;
    return arrow::StringBuilder::Append(value);
  }

  void UnsafeAppend(const char* value, offset_type length) {
    ++actualSize;
    arrow::StringBuilder::UnsafeAppend(value, length);
  }

  void UnsafeAppend(const std::string& value) {
    ++actualSize;
    arrow::StringBuilder::UnsafeAppend(value);
  }

  void UnsafeAppend(arrow::util::string_view value) {
    ++actualSize;
    arrow::StringBuilder::UnsafeAppend(value);
  }

  void ReappendValue(size_t index, ElementType const& value) {
    if(index < actualSize) {
      // this is not a sequential write
      // we would have to move all the data to do this insertion
      // so not supporting it for now
      return;
    }

    auto toStringRef = [](auto const& value) -> std::string const& {
      if constexpr(std::is_same_v<ElementType, Symbol>) {
        return value.getName();
      } else {
        return value;
      }
    };

    auto prevOffset = static_cast<offset_type>(value_data_builder_.length());

    // append the data
    auto const& str = toStringRef(value);
    auto const* strData = reinterpret_cast<uint8_t const*>(str.c_str());
    auto strLength = static_cast<offset_type>(str.size());
    auto status = value_data_builder_.Append(strData, strLength);
    if(!status.ok()) {
      // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
      return;
    }

    // adjust actual size and offsets (including for all values we skipped)
    for(int i = actualSize; i <= index; ++i) {
      offsets_builder_.mutable_data()[i] = prevOffset;
    }

    actualSize = index + 1;
  }

  // need a proxy to pretend having a reference to a string
  // when we actually need to insert when we write on the proxy
  class StringBuilderProxy {
  public:
    StringBuilderProxy(IterableStringBuilder& builder, int index)
        : builder(builder), index(index) {}
    ~StringBuilderProxy() = default;
    StringBuilderProxy(StringBuilderProxy const& other) = default;
    StringBuilderProxy(StringBuilderProxy&& other) noexcept = default;
    bool operator!=(StringBuilderProxy const& rhs) const { return index != rhs.index; }
    bool operator!=(StringBuilderProxy&& rhs) const { return index != rhs.index; }
    StringBuilderProxy operator+(int incr) const {
      return StringBuilderProxy(builder, index + incr);
    }
    void operator++() { ++index; }

    explicit operator ElementType() const {
      return ElementType(std::string(builder.GetView(index)));
    }

    StringBuilderProxy& operator=(ElementType const& value) {
      builder.ReappendValue(index, value);
      return *this;
    }

    StringBuilderProxy& operator=(StringBuilderProxy const& other) {
      *this = static_cast<ElementType>(other);
      return *this;
    }
    StringBuilderProxy& operator=(StringBuilderProxy&& other) noexcept {
      *this = static_cast<ElementType>(other);
      return *this;
    }

  private:
    IterableStringBuilder& builder;
    int index;
  };

  class StringIterator {
  public:
    StringIterator(IterableStringBuilder& builder, int index) : proxy(builder, index) {}
    explicit StringIterator(StringBuilderProxy const& proxy) : proxy(proxy) {}
    StringBuilderProxy const& operator*() const { return proxy; }
    StringBuilderProxy& operator*() { return proxy; }
    bool operator!=(StringIterator const& rhs) const { return proxy != rhs.proxy; }
    bool operator!=(StringIterator&& rhs) const { return proxy != rhs.proxy; }
    StringIterator operator+(size_t incr) const { return StringIterator(proxy + incr); }
    void operator++() { ++proxy; }

  private:
    StringBuilderProxy proxy;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(size_t offset = 0) { return StringIterator(*this, -offset); }
  auto end() { return StringIterator(*this, length()); }

  class StringConstIterator {
  public:
    StringConstIterator(IterableStringBuilder const& builder, int index)
        : builder(builder), index(index) {}
    auto operator*() const { return std::string(builder.GetView(index)); }
    bool operator!=(StringConstIterator const& rhs) const { return index != rhs.index; }
    bool operator!=(StringConstIterator&& rhs) const { return index != rhs.index; }
    StringConstIterator operator+(int incr) const {
      return StringConstIterator(builder, index + incr);
    }
    void operator++() { ++index; }

  private:
    IterableStringBuilder const& builder;
    int index;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(size_t offset = 0) const { return StringConstIterator(*this, -offset); }
  auto end() const { return StringConstIterator(*this, length()); }
};

template <typename T> class IterableNumericBuilder : public arrow::NumericBuilder<T> {
public:
  using value_type = typename arrow::NumericBuilder<T>::value_type;

  explicit IterableNumericBuilder(arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::NumericBuilder<T>(pool) {}

  void SetValue(int64_t index, value_type value) {
    arrow::NumericBuilder<T>::data_builder_.mutable_data()[index] = value;
  }

  value_type const* raw_values() const {
    return reinterpret_cast<value_type const*>(arrow::NumericBuilder<T>::data_builder_.data());
  }
  value_type* raw_values() {
    return reinterpret_cast<value_type*>(arrow::NumericBuilder<T>::data_builder_.mutable_data());
  }

  /// Just wrap an iterator around a pointer to the underline c-array
  class NumericIterator {
  public:
    explicit NumericIterator(value_type* pointer) : pointer(pointer) {}
    value_type& operator*() const { return *pointer; }
    bool operator!=(NumericIterator const& rhs) const { return pointer != rhs.pointer; }
    bool operator!=(NumericIterator&& rhs) const { return pointer != rhs.pointer; }
    NumericIterator operator+(size_t incr) const { return NumericIterator(pointer + incr); }
    void operator++() { ++pointer; }

  private:
    value_type* pointer;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(size_t offset = 0) { return NumericIterator(raw_values() - offset); }
  auto end() { return NumericIterator(raw_values() + arrow::NumericBuilder<T>::length()); }

  /// Just wrap an iterator around a pointer to the underline c-array
  class NumericConstIterator {
  public:
    explicit NumericConstIterator(value_type const* pointer) : pointer(pointer) {}
    value_type const& operator*() const { return *pointer; }
    bool operator!=(NumericConstIterator const& rhs) const { return pointer != rhs.pointer; }
    bool operator!=(NumericConstIterator&& rhs) const { return pointer != rhs.pointer; }
    NumericConstIterator operator+(size_t incr) const {
      return NumericConstIterator(pointer + incr);
    }
    void operator++() { ++pointer; }

  private:
    value_type const* pointer;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(size_t offset = 0) const { return NumericConstIterator(raw_values() - offset); }
  auto end() const {
    return NumericConstIterator(raw_values() + arrow::NumericBuilder<T>::length());
  }
};

using IterableInt32Builder = IterableNumericBuilder<arrow::Int32Type>;
using IterableFloatBuilder = IterableNumericBuilder<arrow::FloatType>;

} // namespace boss::engines::bulk
