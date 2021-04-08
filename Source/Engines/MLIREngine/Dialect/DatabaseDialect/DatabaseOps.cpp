#include "DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "DatabaseTypes.h"

#define GET_OP_CLASSES
#include "DatabaseOps.cpp.inc"

TupleStreamType mlir::database::GetRelationOp::getTupleStream() {
  return getType().cast<TupleStreamType>();
}