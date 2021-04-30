#pragma once
#include "Engines/MLIREngine/Runtime/Database.hpp"
#include "Engines/MLIREngine/Types/Types.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include <arrow/api.h>
#include <mlir/IR/Types.h>
#include <vector>

namespace boss::mlir::conversion {

TupleStreamUnionType arrowSchemaToTupleStreamType(::mlir::MLIRContext* context, std::shared_ptr<arrow::Schema> schema);
arrow::Schema* tupleStreamTypeToArrowSchema(TupleStreamUnionType& t);

std::shared_ptr<arrow::DataType> mlirTypeToArrowType(::mlir::Type const& type);

::mlir::Type arrowTypeToMLIRType(::mlir::MLIRContext* context, arrow::DataType* type);

boss::mlir::types::RuntimeTypes mlirTypeToRuntimeType(::mlir::Type const& type, bool extractSymbol);

size_t mlirTypeToArrowRawBuffer(arrow::ChunkedArray* array, ::mlir::Type type, int chunk);

} // namespace boss