add_mlir_dialect(MemoryOps memory)

add_mlir_dialect_library(MLIRMemory
  MemoryOps.cpp
  MemoryDialect.cpp

  DEPENDS
  MLIRMemoryOpsIncGen

  LINK_LIBS PUBLIC MLIRIR
)

set_property(TARGET obj.MLIRMemory PROPERTY CXX_STANDARD 17)
target_include_directories(obj.MLIRMemory SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)