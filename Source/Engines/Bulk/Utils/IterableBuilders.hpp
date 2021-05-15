#pragma once

#include <arrow/array/builder_binary.h>
#include <arrow/array/builder_primitive.h>
#include <arrow/util/bit_util.h>

namespace boss::engines::bulk {

class IterableBooleanBuilder : public arrow::BooleanBuilder {
  // can these builders not be templatized? If not, we should document why not
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

  class BoolIterator;
  class BoolBuilderProxy {
    friend class BoolIterator;

  public:
    BoolBuilderProxy(IterableBooleanBuilder& builder, size_t index)
        : m_builder(builder), m_index(index) {}
    explicit operator bool() const { return m_builder.GetValue(m_index); }
    BoolBuilderProxy& operator=(bool value) {
      m_builder.SetValue(m_index, value);
      return *this;
    }

  private:
    IterableBooleanBuilder& m_builder;
    size_t m_index;
  };

  class BoolIterator {
  public:
    BoolIterator(IterableBooleanBuilder& builder, size_t index) : m_proxy(builder, index) {}
    auto& operator*() { return m_proxy; }
    bool operator!=(BoolIterator const& rhs) const {
      return m_proxy.m_index != rhs.m_proxy.m_index;
    }
    bool operator!=(BoolIterator&& rhs) const { return m_proxy.m_index != rhs.m_proxy.m_index; }
    BoolIterator operator+(size_t incr) const {
      return BoolIterator(m_proxy.m_builder, m_proxy.m_index + incr);
    }
    void operator++() { ++m_proxy.m_index; }

  private:
    BoolBuilderProxy m_proxy;
  };

  auto begin() { return BoolIterator(*this, 0); }
  auto end() { return BoolIterator(*this, length()); }

  class BoolConstIterator {
  public:
    BoolConstIterator(IterableBooleanBuilder const& builder, size_t index)
        : m_builder(builder), m_index(index) {}
    auto operator*() const { return m_builder[m_index]; }
    bool operator!=(BoolConstIterator const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(BoolConstIterator&& rhs) const { return m_index != rhs.m_index; }
    BoolConstIterator operator+(size_t incr) const {
      return BoolConstIterator(m_builder, m_index + incr);
    }
    void operator++() { ++m_index; }

  private:
    IterableBooleanBuilder const& m_builder;
    size_t m_index;
  };

  auto begin() const { return BoolConstIterator(*this, 0); }
  auto end() const { return BoolConstIterator(*this, length()); }
};

class IterableStringBuilder : public arrow::StringBuilder {
public:
  explicit IterableStringBuilder(arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StringBuilder(pool) {}

  // TODO: what to do with strings?
  // Let's open a ticket
  // we should have reserve+insert instead of resize+set
  // need one of those changes:
  // - a separate StringBatch with a different API
  // - change all batches to be reserve+insert (but how to be still efficient?)
  // - use a dictionary for strings, so can separate ordered insert and data allocation
  class StringBuilderProxy {
  public:
    StringBuilderProxy(IterableStringBuilder& builder, size_t index)
        : m_builder(builder), m_index(index) {}
    ~StringBuilderProxy() = default;
    StringBuilderProxy(StringBuilderProxy const& other) = default;
    StringBuilderProxy(StringBuilderProxy&& other) = default;
    bool operator!=(StringBuilderProxy const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(StringBuilderProxy&& rhs) const { return m_index != rhs.m_index; }
    StringBuilderProxy operator+(size_t incr) const {
      return StringBuilderProxy(m_builder, m_index + incr);
    }
    void operator++() { ++m_index; }

    explicit operator std::string() const { return std::string(m_builder.GetView(m_index)); }

    StringBuilderProxy& operator=(std::string const& value) {
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
      auto status = m_builder.Append(value);
      if(!status.ok()) {
        return *this;
      }
      // TODO: ideally should be able to do this following and reserve size,
      // but it works only if we can ReserveData() as well for binary arrays:
      // assuming it has been reserved!
      // m_builder.UnsafeAppend(value);
      return *this;
    }

    StringBuilderProxy& operator=(Symbol const& value) {
      // not sure I like this! Did this ever come up as a case?
      *this = value.getName();
      return *this;
    }

    StringBuilderProxy& operator=(StringBuilderProxy const& other) {
      *this = static_cast<std::string>(other);
      return *this;
    }
    StringBuilderProxy& operator=(StringBuilderProxy&& other) noexcept {
      *this = static_cast<std::string>(other);
      return *this;
    }

  private:
    IterableStringBuilder& m_builder;
    size_t m_index;
  };

  class StringIterator {
  public:
    StringIterator(IterableStringBuilder& builder, size_t index) : m_proxy(builder, index) {}
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

  auto begin() { return StringIterator(*this, 0); }
  auto end() { return StringIterator(*this, length()); }

  class StringConstIterator {
  public:
    StringConstIterator(IterableStringBuilder const& builder, size_t index)
        : m_builder(builder), m_index(index) {}
    auto operator*() const { return std::string(m_builder.GetView(m_index)); }
    bool operator!=(StringConstIterator const& rhs) const { return m_index != rhs.m_index; }
    bool operator!=(StringConstIterator&& rhs) const { return m_index != rhs.m_index; }
    StringConstIterator operator+(size_t incr) const {
      return StringConstIterator(m_builder, m_index + incr);
    }
    void operator++() { ++m_index; }

  private:
    IterableStringBuilder const& m_builder;
    size_t m_index;
  };

  auto begin() const { return StringConstIterator(*this, 0); }
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

  class PointerIterator {
  public:
    explicit PointerIterator(value_type* pointer) : m_pointer(pointer) {}
    value_type& operator*() const { return *m_pointer; }
    bool operator!=(PointerIterator const& rhs) const { return m_pointer != rhs.m_pointer; }
    bool operator!=(PointerIterator&& rhs) const { return m_pointer != rhs.m_pointer; }
    PointerIterator operator+(size_t incr) const { return PointerIterator(m_pointer + incr); }
    void operator++() { ++m_pointer; }

  private:
    value_type* m_pointer;
  };

  auto begin() { return PointerIterator(raw_values()); }
  // is this weird? a NumericBuilder that has a PointerIterator?
  auto end() { return PointerIterator(raw_values() + arrow::NumericBuilder<T>::length()); }

  class PointerConstIterator {
  public:
    explicit PointerConstIterator(value_type const* pointer) : m_pointer(pointer) {}
    value_type const& operator*() const { return *m_pointer; }
    bool operator!=(PointerConstIterator const& rhs) const { return m_pointer != rhs.m_pointer; }
    bool operator!=(PointerConstIterator&& rhs) const { return m_pointer != rhs.m_pointer; }
    PointerConstIterator operator+(size_t incr) const {
      return PointerConstIterator(m_pointer + incr);
    }
    void operator++() { ++m_pointer; }

  private:
    value_type const* m_pointer;
  };

  auto begin() const { return PointerConstIterator(raw_values()); }
  auto end() const {
    return PointerConstIterator(raw_values() + arrow::NumericBuilder<T>::length());
  }
};

using IterableInt32Builder = IterableNumericBuilder<arrow::Int32Type>;
using IterableFloatBuilder = IterableNumericBuilder<arrow::FloatType>;

} // namespace boss::engines::bulk
