#pragma once

#include <arrow/chunked_array.h>

namespace boss::engines::bulk {

class MutableChunkedArray : public arrow::ChunkedArray {
public:
  MutableChunkedArray() : arrow::ChunkedArray(arrow::ArrayVector{}, nullptr) {}

  explicit MutableChunkedArray(arrow::ArrayVector const& arrays)
      : arrow::ChunkedArray(arrays, nullptr) {
    if(!chunks_.empty()) {
      type_ = chunks_[0]->type();
    }
  }

  explicit MutableChunkedArray(arrow::ArrayVector&& arrays)
      : arrow::ChunkedArray(std::move(arrays), nullptr) {
    if(!chunks_.empty()) {
      type_ = chunks_[0]->type();
    }
  }

  MutableChunkedArray(MutableChunkedArray const& other)
      : arrow::ChunkedArray(other.chunks_, other.type_) {}
  MutableChunkedArray(MutableChunkedArray&& other) noexcept
      : arrow::ChunkedArray(std::move(other.chunks_), std::move(other.type_)) {}

  ~MutableChunkedArray() = default;
  MutableChunkedArray& operator=(MutableChunkedArray const& other) = delete;
  MutableChunkedArray& operator=(MutableChunkedArray&& other) = delete;

  void reserve(size_t chunkSize) { chunks_.reserve(chunkSize); }

  void append(MutableChunkedArray const& other) {
    chunks_.insert(chunks_.begin(), other.chunks_.begin(), other.chunks_.end());
    if(!type_) {
      type_ = other.type_;
    }
  }

  void append(MutableChunkedArray&& other) {
    if(chunks_.empty()) {
      chunks_ = std::move(other.chunks_);
    } else {
      chunks_.insert(chunks_.begin(), other.chunks_.begin(), other.chunks_.end());
    }
    if(!type_) {
      type_ = std::move(other.type_);
    }
  }

  void append(std::shared_ptr<arrow::Array> const& chunk) {
    if(!type_) {
      type_ = chunk->type();
    }
    length_ += chunk->length();
    null_count_ += chunk->null_count();
    chunks_.emplace_back(chunk);
  }

  void append(std::shared_ptr<arrow::Array>&& chunk) {
    if(!type_) {
      type_ = chunk->type();
    }
    length_ += chunk->length();
    null_count_ += chunk->null_count();
    chunks_.emplace_back(std::move(chunk));
  }

  void clear() {
    chunks_.clear();
    length_ = 0;
    null_count_ = 0;
    type_.reset();
  }
};

} // namespace boss::engines::bulk
