add_mlir_interface(TypeInferenceInterface
  DEPENDS
  MLIRTypeInferenceInterfaceIncGen
  LINK_LIBS PUBLIC
	MLIRIR
)

add_subdirectory(MemoryDialect)
add_subdirectory(SExprDialect)
