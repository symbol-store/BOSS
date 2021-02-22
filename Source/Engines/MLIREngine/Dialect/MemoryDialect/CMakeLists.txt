add_mlir_dialect(MemoryOps memory)

add_mlir_dialect_library(MLIRMemory
  MemoryOps.cpp
  MemoryDialect.cpp

  DEPENDS
  MLIRMemoryOpsIncGen

  LINK_LIBS PUBLIC MLIRIR
)
