#include "SExprOps.h"
#include "SExprTypes.h"

#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/TypeInferenceInterface.h"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include "Engines/MLIREngine/Types/TypeInference.hpp"
#include <exception>
#include <iostream>
#include <map>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/Types.h>

#define GET_OP_CLASSES
#include "SExprOps.cpp.inc"

#include "TypeInferenceInterface.cpp.inc"

using boss::mlir::inference::TypeInferenceContext;

// ================ Builders =====================================

void mlir::sexpr::SymbolOp::build(::mlir::OpBuilder& odsBuilder, ::mlir::OperationState& odsState,
                                  std::string name, mlir::ValueRange vals) {
  odsState.addAttribute("name", odsBuilder.getStringAttr(name));
  odsState.addOperands(vals);
  odsState.addTypes(
      odsBuilder.getType<SymbolOrValueType, sexprtype::SymbolOrValue, llvm::Optional<Type>>(
          sexprtype::SymbolOrValue::UNKNOWN, llvm::Optional<Type>{}));
}

// ================ Type Inference ===============================

// ==== Functions to infer type of operator results ====

// ==== Entry point functions for type inference =======

void mlir::sexpr::IntegerConstantOp::inferType(TypeInferenceContext* context) {
  auto currentType = getResult().getType().cast<SymbolOrValueType>();

  auto newType = SymbolOrValueType::get(currentType.getContext(), sexprtype::SymbolOrValue::VALUE,
                                        currentType.getBaseType());

  this->getResult().setType(newType);
}

void mlir::sexpr::SymbolOp::inferType(TypeInferenceContext* context) {
  auto* previousSymbol = context->symbolOp;
  context->symbolOp = this;

  // Update context
  boss::mlir::inference::updateContext(this, *context);

  // Recurse
  for(auto i = getNumOperands(); i > 0; i--) {
    auto arg = getOperand(i - 1);
    arg.getDefiningOp<TypeInference>().inferType(context);
  }

  // Set own type
  std::vector<::mlir::Type> operandTypes(getOperandTypes().begin(), getOperandTypes().end());
  auto resultType = boss::mlir::inference::inferSymbolType(
      this->name().str(), operandTypes, *context);

  context->symbolOp = previousSymbol;
  this->getResult().setType(resultType);
}

void mlir::sexpr::StringConstantOp::inferType(TypeInferenceContext* context) {
  auto currentType = getResult().getType().cast<SymbolOrValueType>();

  auto length = value().size();

  auto newType = SymbolOrValueType::get(currentType.getContext(), sexprtype::SymbolOrValue::VALUE,
                                        StringType::get(currentType.getContext(), length));

  this->getResult().setType(newType);
}

void mlir::sexpr::CombineOp::inferType(TypeInferenceContext* context) {
  auto head = getRegion().front().back().getOperand(0).getDefiningOp<SymbolOp>();
  head.inferType(context);
  mlir::dyn_cast<TypeInference>(getRegion().front().getTerminator()).inferType(context);
}

void mlir::sexpr::EndOp::inferType(TypeInferenceContext* context) {
  auto inputType = this->getOperand().getType().cast<SymbolOrValueType>();

  auto parent = mlir::dyn_cast<sexpr::CombineOp, Operation>(this->getParentOp());

  if(!parent) {
    return;
  }

  parent.getResult().setType(inputType);
}

mlir::sexpr::SymbolOp mlir::sexpr::CombineOp::getHead() {
  return getRegion().front().getTerminator()->getOperand(0).getDefiningOp<SymbolOp>();
}