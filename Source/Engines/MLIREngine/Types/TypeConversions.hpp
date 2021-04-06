#pragma once
#include "Engines/MLIREngine/Runtime/Database.hpp"
#include "Engines/MLIREngine/Types/Types.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include <arrow/api.h>
#include <mlir/IR/Types.h>
#include <vector>

namespace boss::mlir::conversion {

TupleStreamType arrowSchemaToTupleStreamType(::mlir::MLIRContext* context, std::shared_ptr<arrow::Schema> schema);

::mlir::Type arrowTypeToMLIRType(::mlir::MLIRContext* context, arrow::DataType* type);

boss::mlir::types::RuntimeTypes mlirTypeToRuntimeType(::mlir::Type const& type, bool extractSymbol);

} // namespace boss