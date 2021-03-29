add_mlir_dialect(DatabaseOps database)

add_mlir_dialect_library(MLIRDatabase
  DatabaseOps.cpp
  DatabaseDialect.cpp

  DEPENDS
  MLIRDatabaseOpsIncGen

  LINK_LIBS PUBLIC MLIRIR
)
