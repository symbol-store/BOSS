#ifndef SEXPR_OPS
#define SEXPR_OPS

include "MemoryDialect.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/IR/RegionKindInterface.td"
include "TypeInferenceInterface.td"

def SymbolOrValueType : Type<CPred<"$_self.isa<SymbolOrValueType>()">,
                              "symbolOrValue type">;

def PrintMemrefOp : Memory_Op<"printMemref", []> {
  let arguments = (ins AnyMemRef:$ref);
}

def AllocateSymbolOp : Memory_Op<"allocateSymbolOp", []> {
  let arguments = (ins I8MemRef:$name);

  let results = (outs SymbolOrValueType:$symbolPointer);

  let builders = [
    // Build a constant with a given constant floating-point value.
    OpBuilder<"Value nameStringRef", [{
      $_state.addOperands(nameStringRef);
      $_state.addTypes(SymbolOrValueType::get(odsBuilder.getContext(),
        sexprtype::SymbolOrValue::SYMBOL, llvm::Optional<Type>{}));
    }]>
  ];
}

#endif // S
