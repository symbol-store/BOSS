#include "TypeConversions.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Types/TypeInference.hpp"
#include <arrow/api.h>
#include <iostream>
#include <mlir/IR/StandardTypes.h>

namespace boss::mlir::conversion {
using namespace boss::mlir::types;
using namespace mlir::inference;

std::string symbolNameFromSymbolArray(std::shared_ptr<arrow::Array> array) {
  auto typeNameSymbol = std::dynamic_pointer_cast<arrow::StructArray>(array);
  auto typeNameStringDict = std::dynamic_pointer_cast<arrow::DictionaryArray>(typeNameSymbol->field(0));
  auto typeNameStringArray = std::dynamic_pointer_cast<arrow::StringArray>(typeNameStringDict->dictionary());
  auto typeName = typeNameStringArray->GetString(0);
  return typeName;
}

struct ArrowToMlirTypeVisitor : public arrow::TypeVisitor {
  explicit ArrowToMlirTypeVisitor(::mlir::Type& resultType, TypeInferenceContext& context)
      : resultType(resultType), context(context) {}

  arrow::Status Visit(const arrow::NullType& type) override {
    resultType = ::mlir::NoneType::get(context.mlirContext);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::BooleanType& type) override {
    resultType = ::mlir::IntegerType::get(1, context.mlirContext);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::Int8Type& type) override {
    resultType = ::mlir::IntegerType::get(8, context.mlirContext);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::FloatType& type) override {
    resultType = ::mlir::Float32Type::get(context.mlirContext);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::Int32Type& type) override {
    resultType = ::mlir::IntegerType::get(32, context.mlirContext);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::Int64Type& type) override {
    resultType = ::mlir::IndexType::get(context.mlirContext);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::StringType& type) override {
    resultType = StringType::get(context.mlirContext);
    return arrow::Status::OK();
  }
  arrow::Status Visit(const arrow::BinaryType& type) override {
    resultType = SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL,
                                        llvm::Optional<::mlir::Type>());
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::DictionaryType& type) override {
    resultType = SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
    return arrow::Status::OK();
  }

  arrow::Status handleNextValueSymbol() {
    std::vector<::mlir::Type> argumentTypes;
    auto parentArray = std::dynamic_pointer_cast<arrow::StructArray>(context.currentArray);

    // Convert the first argument (local offset)
    ::mlir::Type convertedArgument;
    context.currentArray = parentArray->field(1);
    ArrowToMlirTypeVisitor childVisitor(convertedArgument, context);
    argumentTypes.emplace_back(convertedArgument);
    // Insert the global symbol offset argument
    argumentTypes.emplace_back(::mlir::IndexType::get(context.mlirContext));

    // Load the return type symbol and convert from string to type
    auto symbolTypeName = symbolNameFromSymbolArray(parentArray->field(2));
    auto type = stringToMLIRType(context.mlirContext, symbolTypeName);

    resultType = type;
    return arrow::Status::OK();
  }

  // We have an expression struct, infer the return type
  arrow::Status Visit(const arrow::StructType& type) override {
    auto operationName = type.field(0)->name();
    std::vector<::mlir::Type> argumentTypes;

    if (operationName == "Symbol") {
      // This expression represents a symbol with no arguments
      resultType = SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
      return arrow::Status::OK();
    }
    if (operationName == "NextValue") {
      return handleNextValueSymbol();
    }

    auto parentArray = std::dynamic_pointer_cast<arrow::StructArray>(context.currentArray);

    for(auto i = 1; i < type.num_fields(); i++) {
      context.currentArray = parentArray->field(i);
      ::mlir::Type convertedArgument;
      ArrowToMlirTypeVisitor childVisitor(convertedArgument, context);
      auto status = type.field(i)->type()->Accept(&childVisitor);
      if(!status.ok()) {
        return status;
      }
      argumentTypes.emplace_back(convertedArgument);
    }
    context.currentArray = parentArray;

    resultType = inference::inferSymbolType(operationName, argumentTypes, context);
    if(resultType.isa<SymbolOrValueType>() &&
       resultType.dyn_cast<SymbolOrValueType>().isSymbolic() == sexprtype::SymbolOrValue::VALUE) {
      resultType = resultType.dyn_cast<SymbolOrValueType>().getBaseType();
    }
    return arrow::Status::OK();
  }

  ::mlir::Type& resultType;
  TypeInferenceContext& context;
};

::mlir::Type arrowTypeToMLIRType(inference::TypeInferenceContext& context, arrow::DataType* arrowType) {
  ::mlir::Type mlirType;
  ArrowToMlirTypeVisitor visitor{mlirType, context};
  auto status = arrowType->Accept(&visitor);

  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  return visitor.resultType;
}

std::shared_ptr<arrow::DataType> mlirTypeToArrowType(::mlir::Type const& type) {
  if(type.isInteger(1)) {
    return arrow::boolean();
  } else if(type.isInteger(32)) {
    return arrow::int32();
  } else if(type.isIntOrIndex()) {
    return arrow::int64();
  } else if(type.isIntOrFloat()) {
    return arrow::float32();
  } else if(type.isa<::mlir::MemRefType>() || type.isa<StringType>()) {
    return arrow::binary();
  }
  return {};
}

boss::mlir::types::RuntimeTypes mlirTypeToRuntimeType(::mlir::Type const& type,
                                                      bool extractSymbol) {
  // Check for base types
  if(type.isInteger(1)) {
    return RuntimeTypes::BOOLEAN;
  } else if (type.isIndex()) {
    return RuntimeTypes::INT64;
  } else if(type.isIntOrIndex()) {
    return RuntimeTypes::INT;
  } else if(type.isIntOrFloat()) {
    return RuntimeTypes::FLOAT;
  } else if(type.isa<::mlir::MemRefType>() || type.isa<StringType>()) {
    return RuntimeTypes::STRING;
  } else if(type.isa<TupleStreamUnionType>()) {
    return RuntimeTypes::TUPLE_STREAM;
  } else if(type.isa<RelationType>()) {
    return RuntimeTypes::RELATION;
  }

  // Its not a base type, try to cast it to a symbolOrValueType
  auto symbolOrValue = type.dyn_cast_or_null<SymbolOrValueType>();
  if(!symbolOrValue) {
    return RuntimeTypes::ERROR;
  }

  // Is it symbolic?
  if(symbolOrValue.isSymbolic() == sexprtype::SymbolOrValue::SYMBOL) {
    return RuntimeTypes::SYMBOL;
  }

  // Its not symbolic. Do we care about the type contained in the non-symbolic case?
  if(!extractSymbol) {
    return RuntimeTypes::ERROR;
  }

  auto baseType = symbolOrValue.getBaseType();

  // Extract the type from the base type
  return mlirTypeToRuntimeType(baseType, true);
}

size_t mlirTypeToArrowRawBuffer(arrow::ChunkedArray* array, ::mlir::Type type, int chunk) {
  if(type.isInteger(1)) {
    // Note: Casting to int8 instead of boolean: Packed storage
    return reinterpret_cast<size_t>(
        std::static_pointer_cast<arrow::Int8Array>(array->chunk(chunk))->raw_values());
  } else if(type.isIntOrIndex()) {
    return reinterpret_cast<size_t>(
        std::static_pointer_cast<arrow::Int32Array>(array->chunk(chunk))->raw_values());
  } else if(type.isIntOrFloat()) {
    return reinterpret_cast<size_t>(
        std::static_pointer_cast<arrow::FloatArray>(array->chunk(chunk))->raw_values());
  } else if(type.isa<::mlir::MemRefType>() || type.isa<StringType>()) {
    return reinterpret_cast<size_t>(
        std::static_pointer_cast<arrow::StringArray>(array->chunk(chunk))->raw_data());
  } else if(type.isa<SymbolOrValueType>()) {
    return reinterpret_cast<size_t>(
        std::static_pointer_cast<arrow::BinaryArray>(array->chunk(chunk))->raw_data());
  }
  return 0;
}

std::map<std::string, boss::mlir::types::RuntimeTypes>
mlirFieldsToRuntimeFields(const std::map<std::string, ::mlir::Type>& fields) {
  std::map<std::string, boss::mlir::types::RuntimeTypes> result;

  for(auto const& [name, type] : fields) {
    result[name] = mlirTypeToRuntimeType(type, false);
  }

  return result;
}

::mlir::Type stringToMLIRType(::mlir::MLIRContext* context, std::string typeName) {
  if (typeName == "Int") {
    return ::mlir::IntegerType::get(32, context);
  }
  if (typeName == "Bool") {
    return ::mlir::IntegerType::get(1, context);
  }
  if (typeName == "String") {
    return StringType::get(context);
  }
  if (typeName == "Index") {
    return ::mlir::IndexType::get(context);
  }
  if (typeName == "Symbol") {
    return SymbolOrValueType::get(context, sexprtype::SymbolOrValue::SYMBOL, {});
  }
  return ::mlir::Type{};
}

} // namespace boss::mlir::conversion