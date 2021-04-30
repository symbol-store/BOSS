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

#include "Engines/MLIREngine/Runtime/Database.hpp"

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

void mlir::sexpr::IntegerConstantOp::inferType(new_runtime::Database const& database) {
  auto currentType = getResult().getType().cast<SymbolOrValueType>();

  auto newType = SymbolOrValueType::get(currentType.getContext(), sexprtype::SymbolOrValue::VALUE,
                                        currentType.getBaseType());

  this->getResult().setType(newType);
}

void mlir::sexpr::SymbolOp::inferType(new_runtime::Database const& database) {
  const auto& types = this->getOperandTypes();

  // Verify that we have a symbol or value type
  bool hasSymbol = false;
  for(const auto& someType : types) {
    auto type = someType.dyn_cast<SymbolOrValueType>();

    if(!type) {
      throw std::runtime_error("Expected Symbol or Value Type");
    }

    hasSymbol = hasSymbol || type.isSymbolic() == sexprtype::SymbolOrValue::SYMBOL;
  }

  sexprtype::SymbolOrValue symOrVal =
      hasSymbol ? sexprtype::SymbolOrValue::SYMBOL : sexprtype::SymbolOrValue::VALUE;

  // Infer base type
  if (!boss::mlir::inference::isRegisteredSymbol(this->name().str())) {
    this->getResult().setType(SymbolOrValueType::get(
        this->getContext(), sexprtype::SymbolOrValue::SYMBOL, llvm::Optional<Type>{}));
    return;
  }

  auto baseType = boss::mlir::inference::inferSymbolType(*this, symOrVal, database);

  // TODO is this right?
  if(baseType.isa<SymbolOrValueType>()) {
    this->getResult().setType(baseType);
  } else {
    auto newType = SymbolOrValueType::get(this->getContext(), symOrVal, baseType);
    this->getResult().setType(newType);
  }
}

void mlir::sexpr::StringConstantOp::inferType(new_runtime::Database const& database) {
  auto currentType = getResult().getType().cast<SymbolOrValueType>();

  auto length = value().size();

  auto newType = SymbolOrValueType::get(currentType.getContext(), sexprtype::SymbolOrValue::VALUE,
                                        StringType::get(currentType.getContext(), length));

  this->getResult().setType(newType);
}

void mlir::sexpr::CombineOp::inferType(new_runtime::Database const& database) {
  for(auto& child : getRegion().front().getOperations()) {
    mlir::dyn_cast<TypeInference, Operation>(&child).inferType(database);
  }
}

void mlir::sexpr::EndOp::inferType(new_runtime::Database const& database) {
  auto inputType = this->getOperand().getType().cast<SymbolOrValueType>();

  auto parent = mlir::dyn_cast<sexpr::CombineOp, Operation>(this->getParentOp());

  if(!parent) {
    return;
  }

  parent.getResult().setType(inputType);
}
