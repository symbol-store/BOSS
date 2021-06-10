#pragma once

#include <arrow/array/builder_base.h>
#include <arrow/chunked_array.h>
#include <arrow/type.h>

namespace boss::engines::bulk {

/** This is a simple structure to extract arrays + builder data from any array.
 * It is used for extracting and re-inserting data from/to a CompoundArray. */
class ArrayData {
public:
  ArrayData(arrow::ChunkedArray&& arrays, std::shared_ptr<arrow::ArrayBuilder> builder,
            std::shared_ptr<arrow::Field> field)
      : arrays(std::move(arrays)), builder(std::move(builder)), field(std::move(field)) {}

  ArrayData(arrow::ChunkedArray const& arrays, std::shared_ptr<arrow::ArrayBuilder> builder,
            std::shared_ptr<arrow::Field> field)
      : arrays(arrays.chunks(), arrays.type()), builder(std::move(builder)),
        field(std::move(field)) {}

  arrow::ChunkedArray arrays;
  std::shared_ptr<arrow::ArrayBuilder> builder;
  std::shared_ptr<arrow::Field> field;
};

} // namespace boss::engines::bulk
