#ifndef DATABASE_OPS
#define DATABASE_OPS

include "DatabaseDialect.td"
include "mlir/IR/OpBase.td"

def RelationType : Type<CPred<"$_self.isa<RelationType>()">,
                              "Type relationType">;

def StringType : Type<CPred<"$_self.isa<StringType>()">>;

def GetRelationOp : Database_Op<"getRelation", []> {
  let arguments = (ins
      StrAttr:$relationName
    );

    let results = (outs
      RelationType:$relation
    );

    let builders = [
      OpBuilder<"std::string name, Type databaseResultType", [{
        $_state.addAttribute("relationName", $_builder.getStringAttr(name));
        $_state.addTypes(databaseResultType);
      }]>
    ];
}


#endif
