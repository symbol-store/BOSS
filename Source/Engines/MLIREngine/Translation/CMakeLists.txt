get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)

add_mlir_conversion_library(SExprToStd
  LowerToFunctions.cpp
  LowerToLLVM.cpp
  LowerToStd.cpp

  LINK_LIBS PUBLIC
  ${dialect_libs}
  MLIRIR
  MLIRTranslation
)
