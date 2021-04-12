#ifndef DATABASE_OPS
#define DATABASE_OPS

include "DatabaseDialect.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def TupleStreamType : Type<CPred<"$_self.isa<TupleStreamType>()">,
                              "Type relationType">;

def RelationType : Type<CPred<"$_self.isa<RelationType>()">>;

def StringType : Type<CPred<"$_self.isa<StringType>()">>;

def GetRelationOp : Database_Op<"getRelation", []> {
  let arguments = (ins
      StrAttr:$relationName
    );

    let results = (outs
      TupleStreamType:$relation
    );

    let builders = [
      OpBuilder<"std::string name, TupleStreamType databaseResultType", [{
        $_state.addAttribute("relationName", $_builder.getStringAttr(name));
        $_state.addTypes(databaseResultType);
      }]>
    ];

    code extraClassDeclaration = "TupleStreamType getTupleStream();";
}

def CollectTuplesOp : Database_Op<"collectTuples", []> {
  let arguments = (ins
    TupleStreamType:$inputStream
  );

  let results = (outs
    RelationType:$resultRelation
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


#endif
