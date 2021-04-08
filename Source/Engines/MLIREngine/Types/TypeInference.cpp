#include "TypeInference.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "TypeConversions.hpp"
#include <map>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/Types.h>

namespace boss::mlir::inference {

static const auto inferArithmeticType = [](::mlir::sexpr::SymbolOp& symbol, auto /*sOrV*/,
                                           runtime::Database const& /*database*/) {
  auto const& types = symbol.getOperandTypes();
  ::mlir::Optional<::mlir::Type> baseType;
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

static const auto inferBooleanCompareFunction = [](auto& symbol, auto /*sOrV*/,
                                                   runtime::Database const& database) {
  auto const& types = symbol.getOperandTypes();
  ::mlir::Optional<::mlir::Type> baseType;
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

  return ::mlir::IntegerType::get(1, symbol.getContext());
};

// Returns base type that was inferred
const std::map<std::string,
               std::function<::mlir::Type(::mlir::sexpr::SymbolOp&, sexprtype::SymbolOrValue,
                                          runtime::Database const&)>>
    operatorToType{{"Plus", inferArithmeticType},
                   {"Minus", inferArithmeticType},
                   {"Mul", inferArithmeticType},
                   {"Div", inferArithmeticType},
                   {"Greater", inferBooleanCompareFunction},
                   {"Symbol",
                    [](auto& symbol, auto /*sOrV*/, auto const& /*database*/) {
                      return SymbolOrValueType::get(symbol.getContext(),
                                                    sexprtype::SymbolOrValue::SYMBOL,
                                                    llvm::Optional<::mlir::Type>{});
                    }},
                   {"StringJoin",
                    [](auto& symbol, auto sOrV, auto const& /*database*/) {
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
                    }},
                   {"GetRelation",
                    [](::mlir::sexpr::SymbolOp& symbol, auto sOrV,
                       runtime::Database const& database) -> ::mlir::Type {
                      // The first argument must be a string constant!
                      auto firstOperand = symbol.getOperands().begin();
                      if(firstOperand == symbol.getOperands().end()) {
                        // error: no operands
                        return ::mlir::NoneType();
                      }

                      auto stringOp = ::mlir::dyn_cast_or_null<::mlir::sexpr::StringConstantOp>(
                          (*firstOperand).getDefiningOp());

                      if(stringOp == nullptr) {
                        // error: Operand is not a string constant
                        return ::mlir::NoneType();
                      }

                      auto relationName = stringOp.value();
                      auto table = database.getRelation(std::string(relationName));
                      auto tupleStreamType = boss::mlir::conversion::arrowSchemaToTupleStreamType(
                          symbol.getContext(), table.getSchema());

                      return SymbolOrValueType::get(symbol.getContext(), sOrV, tupleStreamType);
                    }},
                   {"CollectTuples",
                    [](::mlir::sexpr::SymbolOp& symbol, auto sOrV,
                       runtime::Database const& database) -> ::mlir::Type {
                      // TODO change to correct type
                      return RelationType::get(symbol.getContext());
                    }}};

bool isRegisteredSymbol(std::string const& name) {
  return operatorToType.find(name) != operatorToType.end();
}

::mlir::Type inferSymbolType(::mlir::sexpr::SymbolOp& s, sexprtype::SymbolOrValue symOrVal,
                             runtime::Database const& database) {
  // PRE: The operator exists. Use isRegisteredSymbol first.
  auto inferenceFuncIterator = operatorToType.find(s.name().str());
  return (inferenceFuncIterator->second)(s, symOrVal, database);
}

} // namespace boss::mlir::inference