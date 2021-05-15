#pragma once

#include "../Utils/BatchData.hpp"
#include "../Utils/CompoundArray.hpp"
#include "../Utils/UniqueTypeId.hpp"

#include "../../../Expression.hpp"

#include <arrow/type_fwd.h>

#include <memory>

namespace boss::engines::bulk {

class Batch;

// [ISSUE] likely unnecessary. see issue.
/** This is acting like a shared ptr but additional keeping track of the writable property
 * It means that a ReadablePtr is providing only a const Batch
 * and then we can explicitely convert to WritablePtr if we want a non-const Batch:
 * - if the Readableptr encapsulate a WritablePtr then the conversion do nothing
 * - otherwise  it would have to copy first the batch (deep copy) to make it writable
 * The result is we ensure to make a copy only once.
 * Now the question is we ever need to make a batch writable... */
template <typename T> class ReadableBatchPtr {
public:
  using BatchType = T;

  virtual ~ReadableBatchPtr() = default;
  ReadableBatchPtr() : m_batchPtr(), m_writable(false) {}
  explicit ReadableBatchPtr(BatchType const* batch)
      : m_batchPtr(const_cast<BatchType*>(batch)), m_writable(false) {} // NOLINT

  // we take shared_ptr rather than unique_ptr because the batch are potentially stored
  // and we only pass along a reference to them, not a copy (as far as we need only const access)
  explicit ReadableBatchPtr(std::shared_ptr<BatchType const> const& batchPtr)
      : m_batchPtr(std::const_pointer_cast<BatchType>(batchPtr)), m_writable(false) {}
  explicit ReadableBatchPtr(std::shared_ptr<BatchType const>&& batchPtr)
      : m_batchPtr(std::move(std::const_pointer_cast<BatchType>(batchPtr))), m_writable(false) {}

  ReadableBatchPtr(ReadableBatchPtr&& other) noexcept = default;
  ReadableBatchPtr& operator=(ReadableBatchPtr&& other) noexcept = default;

  ReadableBatchPtr(ReadableBatchPtr const& other)
      : m_batchPtr(other.m_batchPtr), m_writable(false) {}
  ReadableBatchPtr& operator=(ReadableBatchPtr const& other) {
    if(&other != this) {
      m_batchPtr = other.m_batchPtr;
    }
    m_writable = false;
    return *this;
  }

  // need to be friend with ReadablePtr/WritablePtr of any other Batch type
  // so we can access the underlined shared_ptr
  // without exposing non-const access to it to the ReadablePtr interface
  template <typename Other> friend class ReadableBatchPtr;
  template <typename Other> friend class WritableBatchPtr;

  template <typename Other>
  explicit ReadableBatchPtr(ReadableBatchPtr<Other> const& other)
      : m_batchPtr(std::static_pointer_cast<BatchType>(other.m_batchPtr)), m_writable(false) {}
  template <typename Other> ReadableBatchPtr& operator=(ReadableBatchPtr<Other> const& other) {
    m_batchPtr = std::static_pointer_cast<BatchType>(other.m_batchPtr);
    m_writable = false;
    return *this;
  }

  template <typename Other>
  explicit ReadableBatchPtr(ReadableBatchPtr<Other>&& other)
      : m_batchPtr(std::static_pointer_cast<BatchType>(std::move(other.m_batchPtr))),
        m_writable(std::move(other.m_writable)) {}
  template <typename Other> ReadableBatchPtr& operator=(ReadableBatchPtr<Other>&& other) {
    m_batchPtr = std::static_pointer_cast<BatchType>(std::move(other.m_batchPtr));
    m_writable = std::move(other.m_writable);
    return *this;
  }

  explicit operator bool() const { return (bool)m_batchPtr; }

  void reset() { m_batchPtr.reset(); }

  BatchType const& operator*() const { return *m_batchPtr; }
  BatchType const* get() const { return m_batchPtr.get(); }
  BatchType const* operator->() const { return m_batchPtr.operator->(); }

  bool isWritable() const { return m_writable; }

protected:
  explicit ReadableBatchPtr(BatchType* batch) : m_batchPtr(batch), m_writable(true) {}
  explicit ReadableBatchPtr(std::shared_ptr<BatchType>&& batchPtr)
      : m_batchPtr(std::move(batchPtr)), m_writable(true) {}

  std::shared_ptr<BatchType> sharedPtr() const { return m_batchPtr; }

private:
  // we share the ownership here with a stored batch
  // (unless this is an intermediate batch and then we are sole owner)
  std::shared_ptr<BatchType> m_batchPtr;
  // keep track if the ReadablePtr encapsulate a WritablePtr
  // it allows us to make it a WritablePtr without copying the data
  bool m_writable;
};

template <typename T> class WritableBatchPtr : public ReadableBatchPtr<T> {
public:
  using BatchType = T;

  ~WritableBatchPtr() override = default;
  WritableBatchPtr() = default;
  explicit WritableBatchPtr(BatchType* batch) : ReadableBatchPtr<BatchType>(batch) {}
  explicit WritableBatchPtr(std::shared_ptr<BatchType>&& batchPtr)
      : ReadableBatchPtr<BatchType>(std::move(batchPtr)) {}

  WritableBatchPtr(WritableBatchPtr const& other) = delete;
  WritableBatchPtr& operator=(WritableBatchPtr const& other) = delete;

  WritableBatchPtr(WritableBatchPtr&& other) noexcept = default;
  WritableBatchPtr& operator=(WritableBatchPtr&& other) noexcept = default;

  template <typename Other>
  explicit WritableBatchPtr(WritableBatchPtr<Other> const& other)
      : ReadableBatchPtr<BatchType>(other) {}
  template <typename Other>
  WritableBatchPtr<BatchType>& operator=(WritableBatchPtr<Other> const& other) {
    ReadableBatchPtr<BatchType>::operator=(other);
    return *this;
  }

  template <typename Other>
  explicit WritableBatchPtr(WritableBatchPtr<Other>&& other)
      : ReadableBatchPtr<BatchType>(std::move(other)) {}
  template <typename Other>
  WritableBatchPtr<BatchType>& operator=(WritableBatchPtr<Other>&& other) {
    ReadableBatchPtr<BatchType>::operator=(std::move(other));
    return *this;
  }

  template <typename Other>
  explicit WritableBatchPtr(ReadableBatchPtr<Other> const& other)
      : ReadableBatchPtr<BatchType>(asWritable(other)) {}
  template <typename Other> WritableBatchPtr& operator=(ReadableBatchPtr<Other> const& other) {
    ReadableBatchPtr<BatchType>::operator=(asWritable(other));
    return *this;
  }

  template <typename Other>
  explicit WritableBatchPtr(ReadableBatchPtr<Other>&& other)
      : ReadableBatchPtr<BatchType>(asWritable(ReadableBatchPtr<BatchType>(std::move(other)))) {}
  template <typename Other> WritableBatchPtr& operator=(ReadableBatchPtr<Other>&& other) {
    ReadableBatchPtr<BatchType>::operator=(asWritable(std::move(other)));
    return *this;
  }

  BatchType& operator*() const { return *this->sharedPtr(); }
  BatchType* get() const { return this->sharedPtr().get(); }
  BatchType* operator->() const { return this->sharedPtr().operator->(); }

  /// This function will copy the batch only if it isn't writable
  static WritableBatchPtr asWritable(ReadableBatchPtr<BatchType> const& batchPtr) {
    if(!batchPtr) {
      return WritableBatchPtr<BatchType>();
    }
    if(batchPtr.isWritable()) {
      // just transfer without data copy
      WritableBatchPtr<BatchType> writablePtr(batchPtr.sharedPtr());
      return writablePtr;
    }
    // here we need to copy the data to make it writable
    if constexpr(std::is_same_v<BatchType, Batch>) {
      return batchPtr->clone();
    } else {
      return batchPtr->template cloneAs<BatchType>();
    }
  }

protected:
  explicit WritableBatchPtr(std::shared_ptr<BatchType> const& batchPtr)
      : ReadableBatchPtr<BatchType>(batchPtr) {}
};

/** a class to hold expression data in a columnar format
 * Batch is the interface providing some generic methods for convenience
 * and most importantly access to the typeId() which is by visitor utilities
 * to access the underlined Batch classes and their specific iterators and methods
 */
class Batch : public std::enable_shared_from_this<Batch> {
public:
  // exposed here for convenience.
  // used to hold a new or existing batch when calling evaluate or when cloning a batch
  using WritablePtr = WritableBatchPtr<Batch>;
  using ReadablePtr = ReadableBatchPtr<Batch>;

  virtual ~Batch() = default;
  Batch() = default;

  Batch(Batch const& other) = delete;
  Batch(Batch&& other) = delete;
  Batch& operator=(Batch const& other) = delete;
  Batch& operator=(Batch&& other) = delete;
  
  /// create a full copy of the batch (without knowing the derived batch type)
  virtual WritablePtr clone(bool clear = false) const = 0;

  virtual size_t size() const = 0;
  virtual void resize(size_t size) = 0;

  /// return a unique identifier for the type of the batch class
  virtual UniqueId::type typeId() const = 0;
  /// return a unique identifier for the type stored by the batch
  virtual UniqueId::type elementTypeId() const = 0;

  /// check if this batch is able to store this type of expression
  virtual bool canContain(Expression const& val) const = 0;

  virtual void insert(Expression const& val) = 0;

  virtual BatchData data() const = 0;

  // [ISSUE] cleanup usage of arrow API
  virtual void setOwner(std::shared_ptr<CompoundArray> parentArray, size_t childIndex) = 0;

  /** set outputPtr to the new evaluated batch and return true if an evaluation occurred,
   * otherwise return false and reset outputPtr */
  virtual bool evaluate(ReadablePtr& outputPtr) const = 0;
};

} // namespace boss::engines::bulk
