#include "DatabaseDialect.h"
#include "DatabaseOps.h"

using namespace mlir::database;

void DatabaseDialect::initialize() {
  // clang-format off
  addOperations<
    #define GET_OP_LIST
    #include "DatabaseOps.cpp.inc"
  >();
  // clang-format on
}
