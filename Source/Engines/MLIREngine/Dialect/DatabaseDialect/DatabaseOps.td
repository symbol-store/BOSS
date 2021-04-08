#ifndef DATABASE_OPS
#define DATABASE_OPS

include "DatabaseDialect.td"
include "mlir/IR/OpBase.td"

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


#endif
