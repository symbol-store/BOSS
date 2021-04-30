#include "TypeInference.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "TypeConversions.hpp"
#include <map>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/Types.h>

namespace boss::mlir::inference {

static const auto inferArithmeticType = [](::mlir::sexpr::SymbolOp& symbol, auto /*sOrV*/,
                                           new_runtime::Database const& /*database*/) {
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
                                                   new_runtime::Database const& database) {
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
                                          new_runtime::Database const&)>>
    operatorToType{
        {"Plus", inferArithmeticType},
        {"Minus", inferArithmeticType},
        {"Mul", inferArithmeticType},
        {"Div", inferArithmeticType},
        {"Greater", inferBooleanCompareFunction},
        {"Symbol",
         [](auto& symbol, auto /*sOrV*/, auto const& /*database*/) {
           return SymbolOrValueType::get(symbol.getContext(), sexprtype::SymbolOrValue::SYMBOL,
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
            new_runtime::Database const& database) -> ::mlir::Type {
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
             return SymbolOrValueType::get(symbol.getContext(), sexprtype::SymbolOrValue::SYMBOL,
                                           {});
           }

           auto relationName = stringOp.value();
           auto table = database.getRelation(std::string(relationName));
           auto rawTablePtr = std::dynamic_pointer_cast<arrow::DenseUnionArray>(table.get());
           auto unionType = rawTablePtr->union_type();

           std::vector<TupleStreamType> streamTypes;
           for(auto const& field : unionType->fields()) {
             std::map<std::string, ::mlir::Type> fieldsAndTypes;
             auto structType = std::dynamic_pointer_cast<arrow::StructType>(field->type());
             for(auto const& columnWithType : structType->fields()) {
               // TODO: take into account what type an expression evaluates to
               fieldsAndTypes[columnWithType->name()] = conversion::arrowTypeToMLIRType(
                   symbol.getContext(), columnWithType->type().get());
             }
             streamTypes.emplace_back(TupleStreamType::get(symbol.getContext(), fieldsAndTypes));
           }

           auto openRelationType = TupleStreamUnionType::get(symbol.getContext(), streamTypes, {});

           return SymbolOrValueType::get(symbol.getContext(), sOrV, openRelationType);
         }},
        {"CollectTuples",
         [](::mlir::sexpr::SymbolOp& symbol, auto sOrV, new_runtime::Database const& database)
             -> ::mlir::Type { return RelationType::get(symbol.getContext()); }},
        {"Project",
         [](::mlir::sexpr::SymbolOp& symbol, auto sOrV,
            new_runtime::Database const& database) -> ::mlir::Type {
           // Parse operands: 1. (List ...), 2. TupleStream
           std::vector<::mlir::StringRef> columns;
           auto listCombine = ::mlir::dyn_cast_or_null<::mlir::sexpr::CombineOp>(
               symbol.getOperands().front().getDefiningOp());
           auto stream = symbol.getOperands()[1]
                             .getType()
                             .dyn_cast_or_null<SymbolOrValueType>()
                             .getBaseType()
                             .dyn_cast_or_null<TupleStreamUnionType>();
           if((!listCombine) || (!stream)) {
             symbol.emitError("Unexpected operands");
             return ::mlir::NoneType::get(symbol.getContext());
           }

           // Find the (List ...) symbol in the arguments
           auto symbols = listCombine.getOps<::mlir::sexpr::SymbolOp>();
           if(symbols.empty()) {
             symbol.emitError("No symbol in region");
             return ::mlir::NoneType::get(symbol.getContext());
           }
           auto listSymbol = *symbols.begin();
           if(listSymbol.name() != "List") {
             symbol.emitError("Expecting a list as first argument");
             return ::mlir::NoneType::get(symbol.getContext());
           }

           // Extract all the strings from the list, and append them to the columns vector
           for(auto const& arg : listSymbol.getOperands()) {
             auto stringConst =
                 ::mlir::dyn_cast_or_null<::mlir::sexpr::StringConstantOp>(arg.getDefiningOp());
             if(!stringConst) {
               symbol.emitError("Expecting a string constant in the list");
               return ::mlir::NoneType::get(symbol.getContext());
             }
             columns.push_back(stringConst.value());
           }

           // Filter the TupleStream's columns by the columns in the "columns" vector
           //           OpenRelationTypeStorage::TupleHeader types;
           //            auto oldHeader = stream.getConcreteTupleTypes();
           //            for(auto const& name : columns) {
           //              auto typeIt = std::find_if(oldHeader.begin(), oldHeader.end(),
           //              [&name](auto elem) { return elem.first == name; }); if (typeIt ==
           //              oldHeader.end()) {
           //                symbol.emitError("Error: Column " + name + " does not exist");
           //              }
           //              types.emplace_back(name, typeIt->second);
           //            }
           //
           //            return SymbolOrValueType::get(symbol.getContext(),
           //            sexprtype::SymbolOrValue::VALUE,
           //                                          OpenRelationType::get(symbol.getContext(),
           //                                          types));
           return ::mlir::NoneType::get(symbol.getContext());
         }}};

bool isRegisteredSymbol(std::string const& name) {
  return operatorToType.find(name) != operatorToType.end();
}

::mlir::Type inferSymbolType(::mlir::sexpr::SymbolOp& s, sexprtype::SymbolOrValue symOrVal,
                             new_runtime::Database const& database) {
  // PRE: The operator exists. Use isRegisteredSymbol first.
  auto inferenceFuncIterator = operatorToType.find(s.name().str());
  return (inferenceFuncIterator->second)(s, symOrVal, database);
}

} // namespace boss::mlir::inference