#ifndef SEXPR_OPS
#define SEXPR_OPS

include "MemoryDialect.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/IR/RegionKindInterface.td"
include "../TypeInferenceInterface.td"

def SymbolOrValueType : Type<CPred<"$_self.isa<SymbolOrValueType>()">,
                              "symbolOrValue type">;

def StringType : Type<CPred<"$_self.isa<StringType>()">>;

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

def AllocateSymbolicFunctionOp : Memory_Op<"allocateSymbolicFuncOp", []> {
  let arguments = (ins I8MemRef:$name, Variadic<AnyType>:$args);

  let results = (outs SymbolOrValueType:$expressionPointer);

  let builders = [
    // Build a constant with a given constant floating-point value.
    OpBuilder<"Value nameStringRef, ValueRange args", [{
      $_state.addOperands(nameStringRef);
      $_state.addOperands(args);
      $_state.addTypes(SymbolOrValueType::get(odsBuilder.getContext(),
        sexprtype::SymbolOrValue::SYMBOL, llvm::Optional<Type>{}));
    }]>
  ];
}

def LoadConstantAddressOp : Memory_Op<"loadConstantAddressOp", [NoSideEffect]> {
  let arguments = (ins
    I64Attr:$address,
    Index:$offset
  );

  let results = (outs AnyType:$loadedValue);

  let builders = [
    OpBuilderDAG<(ins "size_t":$address, "Type":$resultType, "Value":$offset), [{
      $_state.addTypes(resultType);
      $_state.addAttribute("address", $_builder.getI64IntegerAttr(address));
      $_state.addOperands(offset);
    }]>
  ];
}

#endif // S
