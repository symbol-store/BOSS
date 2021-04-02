#ifndef SHAPE_INFERENCE_INTERFACE
#define SHAPE_INFERENCE_INTERFACE

include "mlir/IR/OpBase.td"

def TypeInferenceOpInterface : OpInterface<"TypeInference"> {
  let description = [{
    Interface to infer the type of an expression
  }];

  let methods = [
    InterfaceMethod<"Infer and set the output shape for the current operation.",
                    "void", "inferType", (ins "runtime::Database const&":$database)>
  ];
}

#endif
