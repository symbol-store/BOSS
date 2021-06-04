#pragma once
#include "Engines/MLIREngine/IR/MLIRGenerator.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include <Engines/MLIREngine/Types/Types.hpp>
#include <mlir/IR/Module.h>

namespace boss::engines::mlir::compiler {

class Compiler {
  using SymbolTable = std::unordered_map<std::string, boss::Expression>;

public:
  explicit Compiler(new_runtime::Database* database): database(database), symbolTable() {}

  Compiler(new_runtime::Database* database, SymbolTable symbolTable)
      : database(database), symbolTable(std::move(symbolTable)) {}

  Expression evaluate(Expression const& e, bool compileOnly);

  MLIRGenerator compile(boss::Expression const&, ::boss::mlir::types::RuntimeTypes& returnType);

  boss::Expression executeModule(::mlir::OwningModuleRef module, ::boss::mlir::types::RuntimeTypes returnType);

  int64_t runJit(::mlir::ModuleOp module);

private:
  new_runtime::Database* database;
  SymbolTable symbolTable;
};

} // namespace boss::engines::mlir::compiler