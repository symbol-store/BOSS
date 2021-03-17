#pragma once

#include "../Utils/UniqueTypeId.hpp"

#include "../../../Expression.hpp"

#include <memory>

namespace boss::engines::bulk {

class Batch;

template <typename T> class ReadableBatchPtr {
public:
  using BatchType = T;

  virtual ~ReadableBatchPtr() = default;
  ReadableBatchPtr() : m_batchPtr(), m_writable(false) {}
  explicit ReadableBatchPtr(std::shared_ptr<BatchType const> const& batchPtr)
      : m_batchPtr(std::const_pointer_cast<BatchType>(batchPtr)), m_writable(false) {}
  explicit ReadableBatchPtr(std::shared_ptr<BatchType const>&& batchPtr)
      : m_batchPtr(std::move(std::const_pointer_cast<BatchType>(batchPtr))), m_writable(false) {}

  //ReadableBatchPtr(ReadableBatchPtr& other) = delete;
  //ReadableBatchPtr& operator=(ReadableBatchPtr& other) = delete;

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
  std::shared_ptr<BatchType> m_batchPtr;
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

  static WritableBatchPtr asWritable(ReadableBatchPtr<BatchType> const& batchPtr,
                                     bool clear = false) {
    if(!batchPtr) {
      return WritableBatchPtr<BatchType>();
    }
    if(batchPtr.isWritable()) {
      WritableBatchPtr<BatchType> writablePtr(batchPtr.sharedPtr());
      if(clear) {
        writablePtr->clear();
      }
      return writablePtr;
    }
    if constexpr(std::is_same_v<BatchType, Batch>) {
      return batchPtr->clone(clear);
    } else {
      return batchPtr->template cloneAs<BatchType>(clear);
    }
  }

  static WritableBatchPtr asWritable(ReadableBatchPtr<BatchType>&& batchPtr) {
    if(!batchPtr) {
      return WritableBatchPtr<BatchType>();
    }
    if(batchPtr.isWritable()) {
      WritableBatchPtr<BatchType> writablePtr(batchPtr.sharedPtr());
      return writablePtr;
    }
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

class Batch : public std::enable_shared_from_this<Batch> {
public:
  using WritablePtr = WritableBatchPtr<Batch>;
  using ReadablePtr = ReadableBatchPtr<Batch>;

  virtual ~Batch() = default;
  Batch() = default;

  Batch(Batch const& other) = delete;
  Batch(Batch&& other) = delete;
  Batch& operator=(Batch const& other) = delete;
  Batch& operator=(Batch&& other) = delete;

  virtual WritablePtr clone(bool clear = false) const = 0;
  virtual void clear() = 0;

  virtual size_t size() const = 0;

  virtual void reserve(size_t size) = 0;
  virtual void resize(size_t size, Expression const& val) = 0;

  virtual UniqueId::type typeId() const = 0;
  virtual UniqueId::type elementTypeId() const = 0;

  virtual bool isRLE() const = 0;
  virtual bool canContain(Expression const& val) const = 0;

  virtual void insert(Expression const& val) = 0;
  virtual void merge(ReadablePtr&& other) = 0;

  virtual bool evaluate(ReadablePtr& outputPtr) const = 0;
};

} // namespace boss::engines::bulk
