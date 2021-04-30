#include "DatabaseDialect.h"
#include "DatabaseOps.h"
#include "DatabaseTypes.h"
#include <mlir/IR/DialectImplementation.h>

using namespace mlir::database;
using namespace mlir;

void DatabaseDialect::initialize() {
  // clang-format off
  addOperations<
    #define GET_OP_LIST
    #include "DatabaseOps.cpp.inc"
  >();
  // clang-format on

  addTypes<TupleStreamUnionType, TupleStreamType, RelationType>();
}

void mlir::database::DatabaseDialect::printType(Type type, DialectAsmPrinter& printer) const {

  if(auto result = type.dyn_cast_or_null<TupleStreamUnionType>()) {
    printer << "TupleStreamUnion";
  } else if(auto result = type.dyn_cast_or_null<RelationType>()) {
    printer << "Relation";
  } else if(auto result = type.dyn_cast_or_null<TupleStreamType>()) {
    printer << "TupleStream";
  }
}