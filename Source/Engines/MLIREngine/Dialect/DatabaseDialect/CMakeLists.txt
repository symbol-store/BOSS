add_mlir_dialect(DatabaseOps database)

add_mlir_dialect_library(MLIRDatabase
  DatabaseOps.cpp
  DatabaseDialect.cpp

  DEPENDS
  MLIRDatabaseOpsIncGen

  LINK_LIBS PUBLIC MLIRIR
)

set_property(TARGET obj.MLIRDatabase PROPERTY CXX_STANDARD 17)
target_include_directories(obj.MLIRDatabase SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)