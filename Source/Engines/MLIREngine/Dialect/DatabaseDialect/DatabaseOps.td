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

def GenericTupleStreamUnionType : Type<CPred<"$_self.isa<GenericTupleStreamUnionType>()">, "GenericTupleStreamUnionType">;

def GetRelationOp : Database_Op<"getRelation", [NoSideEffect]> {
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

def BuildJoinTableOp : Database_Op<"buildJoinTable", []> {
  let arguments = (ins
    Variadic<TupleStreamType>:$inputStreams,
    StrArrayAttr:$fields,
    I64Attr:$table
  );

  let builders = [
    OpBuilderDAG<(ins "ValueRange":$tupleStreams, "::mlir::ArrayAttr":$fields, "size_t":$table), [{
        $_state.addAttribute("fields", fields);
        $_state.addAttribute("table", $_builder.getI64IntegerAttr(table));
        $_state.addOperands(tupleStreams);
      }]>
  ];
}

def LookupJoinOp : Database_Op<"lookupJoin", [NoSideEffect]> {
  let arguments = (ins
    TupleStreamType:$inputStream,
    I64Attr:$table,
    StrArrayAttr:$leftFields,
    StrArrayAttr:$rightFields,
    I64Attr:$rightTupleStreamIndex
  );

  let results = (outs
    TupleStreamType:$outputStream
  );

  let builders = [
    OpBuilderDAG<(ins "ValueRange":$tupleStreams, "size_t":$table, "size_t":$rightTupleStreamIndex, "Type":$output, "::mlir::ArrayAttr":$leftFields, "::mlir::ArrayAttr":$rightFields), [{
        $_state.addAttribute("rightTupleStreamIndex", $_builder.getI64IntegerAttr(rightTupleStreamIndex));
        $_state.addAttribute("table", $_builder.getI64IntegerAttr(table));
        $_state.addAttribute("leftFields", leftFields);
        $_state.addAttribute("rightFields", rightFields);
        $_state.addOperands(tupleStreams);
        $_state.addTypes(output);
      }]>
  ];
}

def HashValuesOp : Database_Op<"hashValues", [NoSideEffect]> {
  let arguments = (ins
    Variadic<AnyType>:$inputs
  );

  let results = (outs
    Index:$hash
  );
}

def InsertIntoHashTableOp : Database_Op<"insertHashtableOp", []> {
  let arguments = (ins
    Index:$hash,
    Index:$relationOffset,
    I64Attr:$tablePtr,
    I64Attr:$rightTupleStreamIndex
  );
}

def FindInHashTableOp : Database_Op<"findInHashtableOp", [NoSideEffect]> {
  let arguments = (ins
    Index:$hash,
    I64Attr:$tablePtr
  );

  // TODO return all results
  let results = (outs
    Index:$relationOffset
  );

}

def ProjectionOp : Database_Op<"projectionOp", [NoSideEffect]> {
  let arguments = (ins
    TupleStreamType:$inputStream
  );

  let results = (outs
    TupleStreamType:$outputType
  );
}

def SelectionOp : Database_Op<"selectionOp", [NoSideEffect]> {
  let arguments = (ins
    TupleStreamType:$inputStream,
    StrArrayAttr:$fields,
    StrAttr:$selectionFunctionName
  );

  let results = (outs
    TupleStreamType:$outputTupleStream
  );

  let builders = [
    OpBuilderDAG<(ins "Value":$tupleStream, "::mlir::StringAttr":$selectionFunctionName, "::mlir::ArrayAttr":$fields), [{
      $_state.addAttribute("fields", fields);
      $_state.addAttribute("selectionFunctionName", selectionFunctionName);
      $_state.addTypes(tupleStream.getType());
      $_state.addOperands(tupleStream);
    }]>
  ];
}

def GroupByOp : Database_Op<"groupbyOp", [NoSideEffect]> {
  let arguments = (ins
    Variadic<TupleStreamType>:$inputStreams,
    StrArrayAttr:$groupingFields,
    StrArrayAttr:$aggregationFields,
    StrArrayAttr:$aggregationFunctions
  );

  let results = (outs
    RelationType:$output
  );
}

def GroupByInsert : Database_Op<"groupbyInsert", []> {
  let arguments = (ins
    Index:$hash,
    AnyType:$value,
    IndexAttr:$hashAggregatePointer
  );
}

def GroupByGet : Database_Op<"groupbyLookup", [NoSideEffect]> {
  let arguments = (ins
    Index:$hash,
    IndexAttr:$hashAggregatePointer
  );

  let results = (outs
    AnyType:$aggregate
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

def CreateGenericUnionStream : Database_Op<"createGenericUnionStream", [NoSideEffect]> {
  let arguments = (ins
    Variadic<AnyType>:$inputValues
  );

  let results = (outs
    GenericTupleStreamUnionType:$outputUnion
  );
}

def GetElementFromUnion : Database_Op<"getElementFromUnionStream", [NoSideEffect]> {
  let arguments = (ins
    GenericTupleStreamUnionType:$tupleStream,
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

def FinalizeBuilderOp : Database_Op<"finalizeBuilderOp"> {
  let arguments = (ins
    I64Attr:$builderPtr,
    StrAttr:$builderFunctionName
  );

  let results = (outs
    Index:$relationPtr
  );

  let builders = [
    OpBuilderDAG<(ins "size_t":$builderPtr, "std::string":$functionName), [{
      $_state.addAttribute("builderPtr", $_builder.getI64IntegerAttr(builderPtr));
      $_state.addAttribute("builderFunctionName", $_builder.getStringAttr(functionName));
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

  let results = (outs
    Index:$addedValueOffset
  );

  let builders = [
    OpBuilderDAG<(ins "size_t":$structBuilderPtr, "size_t":$unionBuilderPtr, "int8_t":$childId), [{
      $_state.addAttribute("structBuilderPtr", $_builder.getI64IntegerAttr(structBuilderPtr));
      $_state.addAttribute("unionBuilderPtr", $_builder.getI64IntegerAttr(unionBuilderPtr));
      $_state.addAttribute("childId", $_builder.getI8IntegerAttr(childId));
      $_state.addTypes($_builder.getIndexType());
    }]>
  ];
}


#endif
