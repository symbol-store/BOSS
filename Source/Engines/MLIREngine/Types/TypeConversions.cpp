#include "TypeConversions.hpp"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include <mlir/IR/StandardTypes.h>

namespace boss::mlir::conversion {
using namespace boss::mlir::types;

TupleStreamType arrowSchemaToTupleStreamType(MLIRContext* context, std::shared_ptr<arrow::Schema> schema) {
  TupleStreamTypeStorage::KeyTy types;

  for (auto const& field : schema->fields()) {
    types.emplace_back(field->name(), arrowTypeToMLIRType(context, field->type().get()));
  }

  return TupleStreamType::get(context, types);
}

struct ArrowToMlirTypeVisitor : public arrow::TypeVisitor {
  explicit ArrowToMlirTypeVisitor(::mlir::Type& resultType, MLIRContext* context): resultType(resultType), context(context) {}

  arrow::Status Visit(const arrow::NullType& type) override {
    resultType = ::mlir::NoneType::get(context);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::BooleanType& type) override {
    resultType = ::mlir::IntegerType::get(1, context);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::Int8Type& type) override {
    resultType = ::mlir::IntegerType::get(8, context);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::FloatType& type) override {
    resultType = ::mlir::Float32Type::get(context);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::Int32Type& type) override {
    resultType = ::mlir::IntegerType::get(32, context);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::StringType& type) override {
    resultType = StringType::get(context, -1);
    return arrow::Status::OK();
  }

  ::mlir::Type& resultType;
  MLIRContext* context;
};

::mlir::Type arrowTypeToMLIRType(MLIRContext* context, arrow::DataType* arrowType) {
  ::mlir::Type mlirType;
  ArrowToMlirTypeVisitor visitor{mlirType, context};
  auto status = arrowType->Accept(&visitor);

  return visitor.resultType;
}

boss::mlir::types::RuntimeTypes mlirTypeToRuntimeType(::mlir::Type const& type, bool extractSymbol) {
  // Check for base types
  if(type.isInteger(1)) {
    return RuntimeTypes::BOOLEAN;
  } else if(type.isIntOrIndex()) {
    return RuntimeTypes::INT;
  } else if(type.isIntOrFloat()) {
    return RuntimeTypes::FLOAT;
  } else if(type.isa<::mlir::MemRefType>() || type.isa<StringType>()) {
    return RuntimeTypes::STRING;
  }

  // Its not a base type, try to cast it to a symbolOrValueType
  auto symbolOrValue = type.dyn_cast_or_null<SymbolOrValueType>();
  if (!symbolOrValue) {
    return RuntimeTypes::ERROR;
  }

  // Is it symbolic?
  if(symbolOrValue.isSymbolic() == sexprtype::SymbolOrValue::SYMBOL) {
    return RuntimeTypes::SYMBOL;
  }

  // Its not symbolic. Do we care about the type contained in the non-symbolic case?
  if (!extractSymbol) {
    return RuntimeTypes::ERROR;
  }

  auto baseType = symbolOrValue.getBaseType();

  // Extract the type from the base type
  return mlirTypeToRuntimeType(baseType, true);
}

} // namespace boss