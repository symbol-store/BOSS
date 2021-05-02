#ifndef DATABASE_OPS
#define DATABASE_OPS

include "DatabaseDialect.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def TupleStreamUnionType : Type<CPred<"$_self.isa<TupleStreamUnionType>()">,
                              "A type representing the union of several tuple streams">;

def RelationType : Type<CPred<"$_self.isa<RelationType>()">>;

def StringType : Type<CPred<"$_self.isa<StringType>()">>;

def TupleStreamType : Type<CPred<"$_self.isa<TupleStreamType>()">,
                                                    "A single tuple stream">;

def GetRelationOp : Database_Op<"getRelation", []> {
  let arguments = (ins
      StrAttr:$relationName,
      I32Attr:$unionChildIndex
    );

    let results = (outs
      TupleStreamType:$relation
    );

    let builders = [
      OpBuilder<"std::string name, TupleStreamType databaseResultType, uint32_t unionChildIndex", [{
        $_state.addAttribute("relationName", $_builder.getStringAttr(name));
        $_state.addAttribute("unionChildIndex", $_builder.getI32IntegerAttr(unionChildIndex));
        $_state.addTypes(databaseResultType);
      }]>
    ];

    code extraClassDeclaration = "TupleStreamType getTupleStream();";

}

def CollectTuplesOp : Database_Op<"collectTuples", []> {
  let arguments = (ins
    Variadic<TupleStreamType>:$inputStreams
  );

  let results = (outs
    RelationType:$resultRelation
  );
}

def ProjectionOp : Database_Op<"projectionOp", []> {
  let arguments = (ins
    TupleStreamUnionType:$inputStream
  );

  let results = (outs
    TupleStreamUnionType:$outputType
  );
}

def ExtractFieldFromTupleOp : Database_Op<"extractFieldFromTupStream", []> {
  let arguments = (ins
    TupleStreamType:$tupleStream,
    StrAttr:$fieldName
   );

   let results = (outs
    AnyType:$fieldValue
   );

   let builders = [
    OpBuilderDAG<(ins "Value":$tupleStream, "std::string":$fieldName, "Type":$resultType), [{
      $_state.addAttribute("fieldName", $_builder.getStringAttr(fieldName));
      $_state.addTypes(resultType);
      $_state.addOperands(tupleStream);
    }]>
   ];

   let hasCanonicalizer = 1;
}

def PackFieldsIntoTupleOp : Database_Op<"packFieldsIntoTupStream", [NoSideEffect]> {
  let arguments = (ins
    Variadic<AnyType>:$fields
  );

  let results = (outs
    TupleStreamType:$tupleStream
  );

  let builders = [
    OpBuilderDAG<(ins "ValueRange":$values, "TupleStreamType":$resultType), [{
      $_state.addTypes(resultType);
      $_state.addOperands(values);
    }]>
  ];
}


def CreateUnionTupleStream : Database_Op<"createUnionTupleStreamOp", [NoSideEffect]> {
  let arguments = (ins
    Variadic<TupleStreamType>:$inputStreams
  );


  let results = (outs
    TupleStreamUnionType:$unionTupleStream
  );
}

def GetTupleStreamFromUnion : Database_Op<"getTupleStreamFromUnion", [NoSideEffect]> {
  let arguments = (ins
    TupleStreamUnionType:$tupleStream,
    I32Attr:$fieldIndex
  );

  let results = (outs
    AnyType:$fieldValue
  );

  let hasCanonicalizer = 1;
}


def AppendToRelationOp : Database_Op<"appendToRelationOp"> {
  let arguments = (ins
    AnyType:$value,
    I64Attr:$relationBuilderPtr,
    I64Attr:$runtimeType
  );

  let builders = [
    OpBuilderDAG<(ins "Value":$value, "size_t":$relationBuilderPtr, "boss::mlir::types::RuntimeTypes":$runtimeType), [{
      $_state.addOperands(value);
      $_state.addAttribute("relationBuilderPtr", $_builder.getI64IntegerAttr(relationBuilderPtr));
      $_state.addAttribute("runtimeType", $_builder.getI64IntegerAttr((size_t)runtimeType));
    }]>
  ];
}

def FinalizeRelationOp : Database_Op<"finalizeRelationOp"> {
  let arguments = (ins
    I64Attr:$relationBuilderPtr
  );

  let results = (outs
    Index:$relationPtr
  );

  let builders = [
    OpBuilderDAG<(ins "size_t":$relationBuilderPtr), [{
      $_state.addAttribute("relationBuilderPtr", $_builder.getI64IntegerAttr(relationBuilderPtr));
      $_state.addTypes($_builder.getIndexType());
    }]>
  ];
}

def AdvanceBuilderOp : Database_Op<"advanceBuilder"> {
  let arguments = (ins
    I64Attr:$structBuilderPtr,
    I64Attr:$unionBuilderPtr,
    I8Attr:$childId
  );

  let builders = [
    OpBuilderDAG<(ins "size_t":$structBuilderPtr, "size_t":$unionBuilderPtr, "int8_t":$childId), [{
      $_state.addAttribute("structBuilderPtr", $_builder.getI64IntegerAttr(structBuilderPtr));
      $_state.addAttribute("unionBuilderPtr", $_builder.getI64IntegerAttr(unionBuilderPtr));
      $_state.addAttribute("childId", $_builder.getI8IntegerAttr(childId));
    }]>
  ];
}


#endif
