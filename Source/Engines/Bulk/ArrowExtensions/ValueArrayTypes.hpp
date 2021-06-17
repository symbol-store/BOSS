#pragma once

#include "IterableBuilders.hpp"
#include "SymbolArray.hpp"

#include <arrow/type.h>

namespace boss::engines::bulk {

// builder type and array type we use for all the data types supported by ValueArray
template <typename T> struct ValueArrayTypeToArrowType;
template <> struct ValueArrayTypeToArrowType<bool> {
  static constexpr arrow::Type::type arrowTypeId = arrow::Type::BOOL;
  using arrayType = arrow::BooleanArray;
  using builderType = IterableBooleanBuilder;
};
template <> struct ValueArrayTypeToArrowType<int> {
  static constexpr arrow::Type::type arrowTypeId = arrow::Type::INT32;
  using arrayType = arrow::Int32Array;
  using builderType = IterableInt32Builder;
};
template <> struct ValueArrayTypeToArrowType<float> {
  static constexpr arrow::Type::type arrowTypeId = arrow::Type::FLOAT;
  using arrayType = arrow::FloatArray;
  using builderType = IterableFloatBuilder;
};
template <> struct ValueArrayTypeToArrowType<std::string> {
  static constexpr arrow::Type::type arrowTypeId = arrow::Type::STRING;
  using arrayType = arrow::StringArray;
  using builderType = IterableStringBuilder<>;
};
template <> struct ValueArrayTypeToArrowType<Symbol> {
  static constexpr arrow::Type::type arrowTypeId = arrow::Type::EXTENSION;
  using arrayType = SymbolArray;
  using builderType = SymbolArrayBuilder;
};

} // namespace boss::engines::bulk
