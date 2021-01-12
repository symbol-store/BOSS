#ifndef SEXPR_OPS
#define SEXPR_OPS

include "MemoryDialect.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/IR/RegionKindInterface.td"
include "TypeInferenceInterface.td"

def PrintMemrefOp : Memory_Op<"printMemref", []> {
  let arguments = (ins AnyMemRef:$ref);
}

#endif // S
