#pragma once

#include <arrow/array/builder_base.h>
#include <arrow/chunked_array.h>
#include <arrow/type.h>

namespace boss::engines::bulk {

/** This is a simple structure to extract arrays + builder data from a batch.
 * It is particularly used by the CompoundBatch to extract data from a specific column
 * and construct a new (temporary) batch from it. */
class BatchData {
public:
  BatchData(arrow::ChunkedArray&& _arrays, std::shared_ptr<arrow::ArrayBuilder> _builder,
            size_t _builderLogicalSize, std::shared_ptr<arrow::Field> _field)
      : arrays(std::move(_arrays)), builder(std::move(_builder)),
        builderLogicalSize(_builderLogicalSize), field(std::move(_field)) {}

  BatchData(arrow::ChunkedArray const& _arrays, std::shared_ptr<arrow::ArrayBuilder> _builder,
            size_t _builderLogicalSize, std::shared_ptr<arrow::Field> _field)
      : arrays(_arrays.chunks(), _arrays.type()), builder(std::move(_builder)),
        builderLogicalSize(_builderLogicalSize), field(std::move(_field)) {}

  arrow::ChunkedArray arrays;
  std::shared_ptr<arrow::ArrayBuilder> builder;
  std::shared_ptr<arrow::Field> field;
  // [https://github.com/symbol-store/BOSS/issues/88]
  // needed until we can shrink a builder
  size_t builderLogicalSize;
};

} // namespace boss::engines::bulk
