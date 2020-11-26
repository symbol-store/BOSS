#include "SExprOps.h"
#include "SExprTypes.h"

#include "Engines/MLIREngine/Dialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/TypeInferenceInterface.h"
#include <iostream>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/OperationSupport.h>

#define GET_OP_CLASSES
#include "SExprOps.cpp.inc"

#include "TypeInferenceInterface.cpp.inc"

void mlir::sexpr::SymbolOp::build(::mlir::OpBuilder& odsBuilder, ::mlir::OperationState& odsState,
                                  const std::string& name, mlir::ValueRange vals) {

  odsState.addAttribute("name", odsBuilder.getStringAttr(name));
  odsState.addOperands(vals);
  odsState.addTypes(
      odsBuilder.getType<SymbolOrValueType, sexprtype::SymbolOrValue, llvm::Optional<Type>>(
          sexprtype::SymbolOrValue::UNKNOWN, llvm::Optional<Type>{}));
}

void mlir::sexpr::IntegerConstantOp::inferType() {
  auto currentType = getResult().getType().cast<SymbolOrValueType>();

  auto newType = SymbolOrValueType::get(currentType.getContext(), sexprtype::SymbolOrValue::VALUE,
                                        currentType.getBaseType());

  this->getResult().setType(newType);
}

void mlir::sexpr::SymbolOp::inferType() {
  const auto& types = this->getOperandTypes();

  Optional<Type> baseType{};
  bool hasSymbol = false;
  for(const auto& someType : types) {
    auto type = someType.cast<SymbolOrValueType>();
    baseType = type.getBaseType();
    hasSymbol = hasSymbol || type.isSymbolic() == sexprtype::SymbolOrValue::SYMBOL;
  }

  auto currentType = getResult().getType().cast<SymbolOrValueType>();

  sexprtype::SymbolOrValue symOrVal =
      hasSymbol ? sexprtype::SymbolOrValue::SYMBOL : sexprtype::SymbolOrValue::VALUE;

  auto newType = SymbolOrValueType::get(currentType.getContext(), symOrVal, baseType);

  // if(!baseType.hasValue()) {
  //   throw std::runtime_error("Could not infer type");
  // }

  this->getResult().setType(newType);
}

void mlir::sexpr::StringConstantOp::inferType() {
  auto currentType = getResult().getType().cast<SymbolOrValueType>();

  auto length = value().size();

  auto newType = SymbolOrValueType::get(currentType.getContext(), sexprtype::SymbolOrValue::VALUE,
                                        StringType::get(currentType.getContext(), length));

  this->getResult().setType(newType);
}

void mlir::sexpr::CombineOp::inferType() {
  for(auto& child : getRegion().front().getOperations()) {
    mlir::dyn_cast<TypeInference, Operation>(&child).inferType();
  }
}

void mlir::sexpr::EndOp::inferType() {
  auto inputType = this->getOperand().getType().cast<SymbolOrValueType>();

  auto parent = mlir::dyn_cast<sexpr::CombineOp, Operation>(this->getParentOp());

  if(!parent) {
    return;
  }

  auto resultType = parent.getResult().getType();

  auto newType = SymbolOrValueType::get(resultType.getContext(), inputType.isSymbolic(),
                                        inputType.getBaseTypeChecked());

  parent.getResult().setType(newType);
}
