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

def AllocateStringOp : Memory_Op<"allocateStringOp", [NoSideEffect]> {
  let arguments = (ins Index:$length);

  let results = (outs StringType:$allocatedString);

  let builders = [
    OpBuilder<"Value length", [{
      $_state.addOperands(length);
      $_state.addTypes(StringType::get($_builder.getContext()));
    }]>
  ];
}

def StringLengthOp : Memory_Op<"stringLength", [NoSideEffect]> {
  let arguments = (ins StringType:$string);

  let results = (outs Index:$length);

  let builders = [
    OpBuilder<"Value string", [{
      $_state.addOperands(string);
      $_state.addTypes($_builder.getIndexType());
    }]>
  ];
}

def StringCopyOp : Memory_Op<"stringCopy", []> {
  let arguments = (ins StringType:$source, StringType:$dest, Index:$offset);
}

def PointerCast : Memory_Op<"pointerCast", [NoSideEffect]> {
  let arguments = (ins Index:$value);

  let results = (outs AnyType:$result);
}

def StringReferenceOp : Memory_Op<"stringReference", [NoSideEffect]> {
  let arguments = (ins Index:$pointer, Index:$length);

  let results = (outs StringType:$result);
}

def AllocateSymbolOp : Memory_Op<"allocateSymbolOp", [NoSideEffect]> {
  let arguments = (ins StringType:$name);

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

def AllocateSymbolicFunctionOp : Memory_Op<"allocateSymbolicFuncOp", [NoSideEffect]> {
  let arguments = (ins StringType:$name, Variadic<AnyType>:$args);

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
