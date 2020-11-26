#pragma once

#include "mlir/IR/Builders.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/Types.h"

// #include "SExprOpsDialect.h.inc"
namespace mlir {
namespace sexpr {
class SExprDialect : public mlir::Dialect {
public:
  explicit SExprDialect(::mlir::MLIRContext* context)
      : ::mlir::Dialect(getDialectNamespace(), context,
                        ::mlir::TypeID::get<SExprDialect>()) {

    initialize();
  }

  friend class mlir::MLIRContext;

  void initialize();

  // /// A hook used to materialize constant values with the given type.
  // Operation *materializeConstant(OpBuilder &builder, Attribute value,
  // Type type,
  //                                Location loc) override;

  /// Parse an instance of a type registered to the toy dialect.
  // mlir::Type parseType(mlir::DialectAsmParser &parser) const override;

  /// Print an instance of a type registered to the toy dialect.
  void printType(mlir::Type type,
                 mlir::DialectAsmPrinter& printer) const override;

  /// Provide a utility accessor to the dialect namespace. This is used by
  /// several utilities for casting between dialects.
  static llvm::StringRef getDialectNamespace() { return "sexpr"; }
};

} // namespace sexpr
} // namespace mlir
