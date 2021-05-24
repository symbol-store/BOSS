#pragma once

#include <arrow/array/builder_binary.h>
#include <arrow/array/builder_primitive.h>
#include <arrow/util/bit_util.h>

namespace boss::engines::bulk {

/** Special case to iterate a boolean builder.
 * This is because data is compressed into a bit array,
 * so we cannot read and write data the same as we do for other simple data types.
 * It has to be converted in and out using arrow::bitUtil functions. */
class IterableBooleanBuilder : public arrow::BooleanBuilder {
public:
  explicit IterableBooleanBuilder(arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::BooleanBuilder(pool) {}

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
    BoolBuilderProxy(IterableBooleanBuilder& builder, int index)
        : m_builder(builder), m_index(index) {}
    explicit operator bool() const { return m_builder.GetValue(m_index); }
    BoolBuilderProxy& operator=(bool value) {
      m_builder.SetValue(m_index, value);
      return *this;
    }
    IterableBooleanBuilder& getBuilder() const { return m_builder; }
    int getIndex() const { return m_index; }
    void incrementIndex() { ++m_index; }

  private:
    IterableBooleanBuilder& m_builder;
    int m_index;
  };

  class BoolIterator {
  public:
    BoolIterator(IterableBooleanBuilder& builder, int index) : m_proxy(builder, index) {}
    auto& operator*() { return m_proxy; }
    bool operator!=(BoolIterator const& rhs) const {
      return m_proxy.getIndex() != rhs.m_proxy.getIndex();
    }
    bool operator!=(BoolIterator&& rhs) const {
      return m_proxy.getIndex() != rhs.m_proxy.getIndex();
    }
    BoolIterator operator+(int incr) const {
      return BoolIterator(m_proxy.getBuilder(), m_proxy.getIndex() + incr);
    }
    void operator++() { m_proxy.incrementIndex(); }

  private:
    BoolBuilderProxy m_proxy;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(int offset = 0) { return BoolIterator(*this, -offset); }
  auto end() { return BoolIterator(*this, length()); }

  class BoolConstIterator {
  public:
    BoolConstIterator(IterableBooleanBuilder const& builder, int index)
        : m_builder(builder), m_index(index) {}
    auto operator*() const { return m_builder[m_index]; }
    bool operator!=(BoolConstIterator const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(BoolConstIterator&& rhs) const { return m_index != rhs.m_index; }
    BoolConstIterator operator+(int incr) const {
      return BoolConstIterator(m_builder, m_index + incr);
    }
    void operator++() { ++m_index; }

  private:
    IterableBooleanBuilder const& m_builder;
    int m_index;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(int offset = 0) const { return BoolConstIterator(*this, -offset); }
  auto end() const { return BoolConstIterator(*this, length()); }
};

/** Special case to iterate a string builder.
 * This is because we cannot have random write like other simple data types.
 * We can only iterate as read-only and write by appending to the end.
 * Because for now, we need to fit this builder API with other builders,
 * we pretend to resize+set data, but we should do is actually reserve+insert. */
template <typename ElementType = std::string> // allow to handle Symbol as well
class IterableStringBuilder : public arrow::StringBuilder {
  // [https://github.com/symbol-store/BOSS/issues/88] we need one of those changes:
  // - a separate StringBatch with a different API
  // - change all batches to be reserve+insert (but how to be still efficient?)
  // - use a dictionary for strings, so can separate ordered insert and data allocation
public:
  explicit IterableStringBuilder(arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StringBuilder(pool) {}

  // need a proxy to pretend having a reference to a string
  // when we actually need to iterate using the append workaround
  class StringBuilderProxy {
  public:
    StringBuilderProxy(IterableStringBuilder& builder, int index)
        : m_builder(builder), m_index(index) {}
    ~StringBuilderProxy() = default;
    StringBuilderProxy(StringBuilderProxy const& other) = default;
    StringBuilderProxy(StringBuilderProxy&& other) noexcept = default;
    bool operator!=(StringBuilderProxy const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(StringBuilderProxy&& rhs) const { return m_index != rhs.m_index; }
    StringBuilderProxy operator+(int incr) const {
      return StringBuilderProxy(m_builder, m_index + incr);
    }
    void operator++() { ++m_index; }

    explicit operator ElementType() const {
      return ElementType(std::string(m_builder.GetView(m_index)));
    }

    StringBuilderProxy& operator=(ElementType const& value) {
      if(m_builder.length() > m_index) {
        // TODO: need another solution for the storage for random write
        // cannot replace a previously inserted value...
        // (except if it takes exactly the same size?)
        return *this;
      }
      if(m_builder.length() < m_index) {
        auto status = m_builder.AppendEmptyValues(m_index - m_builder.length());
        if(!status.ok()) {
          return *this;
        }
      }

      auto toStringRef = [](auto const& value) -> std::string const& {
        if constexpr(std::is_same_v<ElementType, Symbol>) {
          return value.getName();
        } else {
          return value;
        }
      };

      auto status = m_builder.Append(toStringRef(value));
      if(!status.ok()) {
        // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
        return *this;
      }

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
    IterableStringBuilder& m_builder;
    int m_index;
  };

  class StringIterator {
  public:
    StringIterator(IterableStringBuilder& builder, int index) : m_proxy(builder, index) {}
    explicit StringIterator(StringBuilderProxy const& proxy) : m_proxy(proxy) {}
    StringBuilderProxy const& operator*() const { return m_proxy; }
    StringBuilderProxy& operator*() { return m_proxy; }
    bool operator!=(StringIterator const& rhs) const { return m_proxy != rhs.m_proxy; }
    bool operator!=(StringIterator&& rhs) const { return m_proxy != rhs.m_proxy; }
    StringIterator operator+(size_t incr) const { return StringIterator(m_proxy + incr); }
    void operator++() { ++m_proxy; }

  private:
    StringBuilderProxy m_proxy;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(size_t offset = 0) { return StringIterator(*this, -offset); }
  auto end() { return StringIterator(*this, length()); }

  class StringConstIterator {
  public:
    StringConstIterator(IterableStringBuilder const& builder, int index)
        : m_builder(builder), m_index(index) {}
    auto operator*() const { return std::string(m_builder.GetView(m_index)); }
    bool operator!=(StringConstIterator const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(StringConstIterator&& rhs) const { return m_index != rhs.m_index; }
    StringConstIterator operator+(int incr) const {
      return StringConstIterator(m_builder, m_index + incr);
    }
    void operator++() { ++m_index; }

  private:
    IterableStringBuilder const& m_builder;
    int m_index;
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
    explicit NumericIterator(value_type* pointer) : m_pointer(pointer) {}
    value_type& operator*() const { return *m_pointer; }
    bool operator!=(NumericIterator const& rhs) const { return m_pointer != rhs.m_pointer; }
    bool operator!=(NumericIterator&& rhs) const { return m_pointer != rhs.m_pointer; }
    NumericIterator operator+(size_t incr) const { return NumericIterator(m_pointer + incr); }
    void operator++() { ++m_pointer; }

  private:
    value_type* m_pointer;
  };

  // [https://github.com/symbol-store/BOSS/issues/88] offset is a workaround for now
  // because not all elements are iterable (if they are already in an array).
  // So we use the offset to start in the negative "before" the beginning of the builder array
  auto begin(size_t offset = 0) { return NumericIterator(raw_values() - offset); }
  auto end() { return NumericIterator(raw_values() + arrow::NumericBuilder<T>::length()); }

  /// Just wrap an iterator around a pointer to the underline c-array
  class NumericConstIterator {
  public:
    explicit NumericConstIterator(value_type const* pointer) : m_pointer(pointer) {}
    value_type const& operator*() const { return *m_pointer; }
    bool operator!=(NumericConstIterator const& rhs) const { return m_pointer != rhs.m_pointer; }
    bool operator!=(NumericConstIterator&& rhs) const { return m_pointer != rhs.m_pointer; }
    NumericConstIterator operator+(size_t incr) const {
      return NumericConstIterator(m_pointer + incr);
    }
    void operator++() { ++m_pointer; }

  private:
    value_type const* m_pointer;
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
