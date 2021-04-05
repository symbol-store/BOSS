get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)

add_mlir_conversion_library(SExprToStd
  LowerToFunctions.cpp
  LowerToLLVM.cpp
  LowerToStd.cpp
  LowerToDatabaseOps.cpp

  LINK_LIBS PUBLIC
  ${dialect_libs}
  MLIRIR
  MLIRTranslation
)

set_property(TARGET obj.SExprToStd PROPERTY CXX_STANDARD 17)
target_include_directories(obj.SExprToStd SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
