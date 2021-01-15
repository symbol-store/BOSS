#include "SExprOps.h"
#include "SExprTypes.h"

#include "Engines/MLIREngine/Dialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/TypeInferenceInterface.h"
#include <exception>
#include <iostream>
#include <map>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/Types.h>

#define GET_OP_CLASSES
#include "SExprOps.cpp.inc"

#include "TypeInferenceInterface.cpp.inc"

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

static auto inferArithmeticType = [](mlir::sexpr::SymbolOp& symbol, auto sOrV) {
  auto const& types = symbol.getOperandTypes();
  Optional<Type> baseType;
  // Check all input types are the same
  for(const auto& type : types) {
    // Check that it is an integer
    // Cast is safe as this is already checked
    if(!type.cast<SymbolOrValueType>().getBaseType().isIntOrIndexOrFloat()) {
      throw std::runtime_error("Expected a type for arithmetic operation");
    }
    // Check that all args are of the same type
    if(baseType.hasValue()) {
      if(baseType.getValue() != type) {
        throw std::runtime_error("Expected a type for arithmetic operation");
      }
    } else {
      baseType = type;
    }
  }

  if(!baseType.hasValue()) {
    throw std::runtime_error("Expected a type for arithmetic operation");
  }

  return baseType.getValue().dyn_cast<SymbolOrValueType>().getBaseType();
};

static auto inferBooleanCompareFunction = [](auto& symbol, auto sOrV) {
  auto const& types = symbol.getOperandTypes();
  Optional<Type> baseType;
  // Check all input types are the same
  for(const auto& type : types) {
    // Check that it is an integer
    // Cast is safe as this is already checked
    if(!type.template cast<SymbolOrValueType>().getBaseType().isIntOrFloat()) {
      throw std::runtime_error("Expected a type for arithmetic operation");
    }
    // Check that all args are of the same type
    if(baseType.hasValue()) {
      if(baseType.getValue() != type) {
        throw std::runtime_error("Expected a type for arithmetic operation");
      }
    } else {
      baseType = type;
    }
  }

  if(!baseType.hasValue()) {
    throw std::runtime_error("Expected a type for arithmetic operation");
  }

  return IntegerType::get(1, symbol.getContext());
};

// Returns base type that was inferred
const std::map<std::string,
               std::function<mlir::Type(mlir::sexpr::SymbolOp&, sexprtype::SymbolOrValue)>>
    operatorToType{{"Plus", inferArithmeticType},
                   {"Minus", inferArithmeticType},
                   {"Mul", inferArithmeticType},
                   {"Div", inferArithmeticType},
                   {"Greater", inferBooleanCompareFunction},
                   {"Symbol",
                    [](auto& symbol, auto sOrV) {
                      return SymbolOrValueType::get(symbol.getContext(),
                                                    sexprtype::SymbolOrValue::SYMBOL,
                                                    llvm::Optional<Type>{});
                    }},
                   {"StringJoin", [](auto& symbol, auto sOrV) {
                      if(sOrV != sexprtype::SymbolOrValue::VALUE) {
                        return StringType::get(symbol.getContext(), 0);
                      };
                      int length = 0;
                      for(const auto& type : symbol.getOperandTypes()) {
                        auto val = type.template dyn_cast<SymbolOrValueType>()
                                       .getBaseType()
                                       .template dyn_cast<StringType>();
                        if(!val) {
                          throw std::runtime_error("Expected a string as argument");
                        }
                        length += val.getLength();
                      }

                      return StringType::get(symbol.getContext(), length);
                    }}};

// ==== Entry point functions for type inference =======

void mlir::sexpr::IntegerConstantOp::inferType() {
  auto currentType = getResult().getType().cast<SymbolOrValueType>();

  auto newType = SymbolOrValueType::get(currentType.getContext(), sexprtype::SymbolOrValue::VALUE,
                                        currentType.getBaseType());

  this->getResult().setType(newType);
}

void mlir::sexpr::SymbolOp::inferType() {
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
  auto inferenceFuncItertor = operatorToType.find(std::string{this->name()});
  if(inferenceFuncItertor == operatorToType.end()) {
    this->getResult().setType(SymbolOrValueType::get(
        this->getContext(), sexprtype::SymbolOrValue::SYMBOL, llvm::Optional<Type>{}));
    return;
  }

  auto baseType = (inferenceFuncItertor->second)(*this, symOrVal);

  // TODO is this right?
  if(baseType.isa<SymbolOrValueType>()) {
    this->getResult().setType(baseType);
  } else {
    auto newType = SymbolOrValueType::get(this->getContext(), symOrVal, baseType);
    this->getResult().setType(newType);
  }
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

  // auto resultType = parent.getResult().getType();

  // auto newType = SymbolOrValueType::get(resultType.getContext(), inputType.isSymbolic(),
  //                                       inputType.getBaseTypeChecked());

  parent.getResult().setType(inputType);
}
