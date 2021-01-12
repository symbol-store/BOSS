#ifndef MEMORY_DIALECT
#define MEMORY_DIALECT

include "mlir/IR/OpBase.td"

//===----------------------------------------------------------------------===//
// Standalone dialect definition.
//===----------------------------------------------------------------------===//

def Memory_Dialect : Dialect {
    let name = "memory";
    let summary = "Memory Dialect";
    let description = [{
        This dialect represents memory operations for s-expression allocation.
    }];
    let cppNamespace = "::mlir::memory";
}

//===----------------------------------------------------------------------===//
// Base standalone operation definition.
//===----------------------------------------------------------------------===//

class Memory_Op<string mnemonic, list<OpTrait> traits = []> :
        Op<Memory_Dialect, mnemonic, traits>;

#endif // STANDALONE_DIALECT
