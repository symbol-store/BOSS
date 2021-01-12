add_mlir_dialect(SExprOps sexpr)

add_mlir_interface(TypeInferenceInterface
  DEPENDS
  MLIRTypeInferenceInterfaceIncGen
  LINK_LIBS PUBLIC
	MLIRIR
)

add_mlir_dialect_library(MLIRSExpr
  SExprOps.cpp
  SExprDialect.cpp
  MemoryOps.cpp
  MemoryDialect.cpp

  DEPENDS
  MLIRSExprOpsIncGen
  MLIRMemoryOpsIncGen

	LINK_LIBS PUBLIC
	MLIRIR
)

add_mlir_dialect(MemoryOps memory)

add_mlir_dialect_library(MLIRMemory
  MemoryOps.cpp
  MemoryDialect.cpp
  SExprOps.cpp
  SExprDialect.cpp

  DEPENDS
  MLIRMemoryOpsIncGen
  MLIRSExprOpsIncGen

  LINK_LIBS PUBLIC MLIRIR
)
