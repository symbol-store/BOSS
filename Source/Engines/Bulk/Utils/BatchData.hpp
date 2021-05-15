#pragma once

#include <arrow/type_fwd.h>

namespace boss::engines::bulk {

/** This is a simple structure to extract arrays + builder data from a batch.
 * It is particularly used by the CompoundBatch to extract data from a specific column
 * and construct a new (temporary) batch from it. */
class BatchData {
public:
  BatchData(arrow::ChunkedArray const& _arrays, std::shared_ptr<arrow::ArrayBuilder> _builder,
            size_t _builderLogicalSize)
      : arrays(_arrays), builder(std::move(_builder)), builderLogicalSize(_builderLogicalSize) {}

  arrow::ChunkedArray const& arrays;
  std::shared_ptr<arrow::ArrayBuilder> builder;
  
  // [ISSUE] (part of arrow API issue) needed until we can shrink a builder
  size_t builderLogicalSize;
};

} // namespace boss::engines::bulk
