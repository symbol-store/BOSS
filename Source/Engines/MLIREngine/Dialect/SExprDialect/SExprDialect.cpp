#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include <mlir/IR/Dialect.h>
#include <mlir/IR/DialectImplementation.h>
#include <mlir/IR/Types.h>

using namespace mlir;
using namespace mlir::sexpr;

void SExprDialect::initialize() {
  // clang-format off
  addOperations<
    #define GET_OP_LIST
    #include "SExprOps.cpp.inc"
  >();

  addTypes<
    SymbolOrValueType,
    StringType
  >();
  // clang-format on
}

void mlir::sexpr::SExprDialect::printType(Type type, DialectAsmPrinter& printer) const {

  if(auto result = type.dyn_cast_or_null<SymbolOrValueType>()) {
    if(result.hasType()) {
      printer.printType(result.getBaseType());
    } else {
      printer << "unknown:";
    }

    switch(result.isSymbolic()) {
    case sexprtype::SymbolOrValue::SYMBOL:
      printer << "sym";
      break;
    case sexprtype::SymbolOrValue::VALUE:
      printer << "val";
      break;
    default:
      printer << "u";
    }
  } else if(auto result = type.dyn_cast<StringType>()) {
    printer << "string";
  }
}
