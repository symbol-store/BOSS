#ifndef STANDALONE_DIALECT
#define STANDALONE_DIALECT

include "mlir/IR/OpBase.td"

//===----------------------------------------------------------------------===//
// Standalone dialect definition.
//===----------------------------------------------------------------------===//

def SExpr_Dialect : Dialect {
    let name = "sexpr";
    let summary = "S-Expression Dialect";
    let description = [{
        This dialect represents S-Expressions.
    }];
    let cppNamespace = "::mlir::sexpr";
}

//===----------------------------------------------------------------------===//
// Base standalone operation definition.
//===----------------------------------------------------------------------===//

class SExpr_Op<string mnemonic, list<OpTrait> traits = []> :
        Op<SExpr_Dialect, mnemonic, traits>;

#endif // STANDALONE_DIALECT
