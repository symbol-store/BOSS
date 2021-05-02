#pragma once

#include <arrow/type_fwd.h>

namespace boss::engines::bulk {

struct BatchData {
  BatchData(arrow::ChunkedArray const& _arrays, std::shared_ptr<arrow::ArrayBuilder> _builder,
            size_t _builderLogicalSize)
      : arrays(_arrays), builder(std::move(_builder)), builderLogicalSize(_builderLogicalSize) {}
  arrow::ChunkedArray const& arrays;
  std::shared_ptr<arrow::ArrayBuilder> builder;
  size_t builderLogicalSize;
};

} // namespace boss::engines::bulk
