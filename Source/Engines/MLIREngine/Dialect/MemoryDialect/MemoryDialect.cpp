#include "MemoryDialect.h"
#include "MemoryOps.h"

using namespace mlir;
using namespace mlir::memory;

void MemoryDialect::initialize() {
  // clang-format off
  addOperations<
    #define GET_OP_LIST
    #include "MemoryOps.cpp.inc"
  >();
  // clang-format on
}
