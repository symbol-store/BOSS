#ifndef SEXPR_OPS
#define SEXPR_OPS

include "SExprDialect.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/IR/RegionKindInterface.td"
include "TypeInferenceInterface.td"

def SymbolOrValueType : Type<CPred<"$_self.isa<SymbolOrValueType>()">,
                              "symbolOrValue type">;

def StringType : Type<CPred<"$_self.isa<StringType>()">>;


def IntegerConstantOp : SExpr_Op<"integerConstant", [DeclareOpInterfaceMethods<TypeInferenceOpInterface>]> {
  let arguments = (ins
    I32Attr:$value
  );

  let results = (outs
    SymbolOrValueType:$outVal
  );

  let builders = [
    // Build a constant with a given constant floating-point value.
    OpBuilder<"int value", [{
      $_state.addAttribute("value", $_builder.getI32IntegerAttr(value));
      $_state.addTypes($_builder.getType<SymbolOrValueType, sexprtype::SymbolOrValue, Type>(
          sexprtype::SymbolOrValue::UNKNOWN, $_builder.getI64Type()));
    }]>
  ];
}

def StringConstantOp : SExpr_Op<"stringConstant", [DeclareOpInterfaceMethods<TypeInferenceOpInterface>]> {
  let arguments = (ins
    StrAttr:$value
  );

  let results = (outs
    SymbolOrValueType:$outVal
  );

  let builders = [
    OpBuilder<"std::string value", [{
      $_state.addAttribute("value", $_builder.getStringAttr(value));
      $_state.addTypes($_builder.getType<SymbolOrValueType, sexprtype::SymbolOrValue, Type>(
          sexprtype::SymbolOrValue::UNKNOWN, $_builder.getType<StringType, size_t>(0)));
    }]>
  ];
}

def SymbolOp: SExpr_Op<"symbol", [DeclareOpInterfaceMethods<TypeInferenceOpInterface>]> {
  let arguments = (ins
    StrAttr:$name,
    Variadic<SymbolOrValueType>:$ins
  );

  let builders = [
    // Build a symbol from the name and inputs.
    OpBuilder<"std::string name, mlir::ValueRange">
  ];

  let results = (outs
    SymbolOrValueType:$output
  );

}

def CombineOp: SExpr_Op<"combine", [DeclareOpInterfaceMethods<TypeInferenceOpInterface>, IsolatedFromAbove]> {
  let regions = (region
    SizedRegion<1>:$expressions
  );

  let results = (outs
    SymbolOrValueType:$output
  );

}

def EndOp: SExpr_Op<"end", [Terminator, DeclareOpInterfaceMethods<TypeInferenceOpInterface>]> {

  let arguments = (ins
    SymbolOrValueType:$value
  );

}


#endif // S
