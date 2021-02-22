add_mlir_dialect(SExprOps sexpr)

add_mlir_dialect_library(MLIRSExpr
  SExprOps.cpp
  SExprDialect.cpp

  DEPENDS
  MLIRSExprOpsIncGen

	LINK_LIBS PUBLIC
	MLIRIR
)
