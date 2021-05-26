#pragma once
#include "Engines/MLIREngine/Runtime/Storage.hpp"

namespace boss::engines::mlir::compiler {

class Compiler {
  using SymbolTable = std::unordered_map<std::string, boss::Expression>;

public:
  explicit Compiler(new_runtime::Database* database): database(database), symbolTable() {}

  Compiler(new_runtime::Database* database, SymbolTable symbolTable)
      : database(database), symbolTable(std::move(symbolTable)) {}

  boss::Expression evaluate(boss::Expression const&);

private:
  new_runtime::Database* database;
  SymbolTable symbolTable;
};

} // namespace boss::engines::mlir::compiler