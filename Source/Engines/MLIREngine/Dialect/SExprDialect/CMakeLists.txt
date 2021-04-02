add_mlir_dialect(SExprOps sexpr)

add_mlir_dialect_library(MLIRSExpr
        SExprOps.cpp
        SExprDialect.cpp

        DEPENDS
        MLIRSExprOpsIncGen
        arrow

        LINK_LIBS PUBLIC
        MLIRIR
)

set_property(TARGET obj.MLIRSExpr PROPERTY CXX_STANDARD 17)
target_include_directories(obj.MLIRSExpr SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
