#pragma once
#include "Engines/MLIREngine/Runtime/Storage.hpp"

namespace boss::engines::mlir::compiler {

class Compiler {
public:
  explicit Compiler(new_runtime::Database* database): database(database) {}

  boss::Expression evaluate(boss::Expression const&);
private:
  new_runtime::Database* database;
};

}