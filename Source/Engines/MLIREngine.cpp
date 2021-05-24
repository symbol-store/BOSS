#include "Engines/MLIREngine.hpp"
#include "Expression.hpp"

namespace boss::engines::mlir {

using namespace boss::mlir::types;

Expression Engine::evaluate(Expression const& e) {
  return interpreter.evaluate(e);
}

} // namespace boss::engines::mlir
