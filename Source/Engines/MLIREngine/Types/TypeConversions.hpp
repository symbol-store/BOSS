#pragma once
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

std::map<std::string, boss::mlir::types::RuntimeTypes> mlirFieldsToRuntimeFields(std::map<std::string, ::mlir::Type> const& fields);

size_t mlirTypeToArrowRawBuffer(arrow::ChunkedArray* array, ::mlir::Type type, int chunk);

::mlir::Type stringToMLIRType(::mlir::MLIRContext* context, std::string typeName);

::mlir::Type runtimeTypeToMLIRType(boss::mlir::types::RuntimeTypes type, ::mlir::MLIRContext* context);

} // namespace boss