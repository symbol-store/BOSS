#ifndef DATABASE_DIALECT
#define DATABASE_DIALECT

include "mlir/IR/OpBase.td"

//===----------------------------------------------------------------------===//
// Standalone dialect definition.
//===----------------------------------------------------------------------===//

def Database_Dialect : Dialect {
    let name = "database";
    let summary = "Database Dialect";
    let description = [{
        This dialect represents database operations.
    }];
    let cppNamespace = "::mlir::database";
}

//===----------------------------------------------------------------------===//
// Base standalone operation definition.
//===----------------------------------------------------------------------===//

class Database_Op<string mnemonic, list<OpTrait> traits = []> :
        Op<Database_Dialect, mnemonic, traits>;

#endif // STANDALONE_DIALECT
