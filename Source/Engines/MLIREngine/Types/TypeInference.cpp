#include "TypeInference.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "TypeConversions.hpp"
#include <iostream>
#include <map>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/Types.h>
#include <set>

namespace boss::mlir::inference {

using sexprtype::SymbolOrValue;

static const auto inferArithmeticType = [](std::vector<::mlir::Type>& argTypes,
                                           TypeInferenceContext& context) -> ::mlir::Type {
  if(hasSymbolicArguments(argTypes)) {
    return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
  }

  ::mlir::Optional<::mlir::Type> baseType;
  // Check all input types are the same
  for(auto& type : argTypes) {
    ::mlir::Type extractedType;
    if(type.isa<SymbolOrValueType>()) {
      extractedType = type.dyn_cast<SymbolOrValueType>().getBaseType();
    } else {
      extractedType = type;
    }

    // Check that it is an integer
    // Cast is safe as this is already checked
    if(!extractedType.isIntOrIndexOrFloat()) {
      throw std::runtime_error("Expected a type for arithmetic operation");
    }
    // Check that all args are of the same type
    if(baseType.hasValue()) {
      if(baseType.getValue() != extractedType) {
        throw std::runtime_error("Expected a type for arithmetic operation");
      }
    } else {
      baseType = extractedType;
    }
  }

  if(!baseType.hasValue()) {
    throw std::runtime_error("Expected a type for arithmetic operation");
  }

  return baseType.getValue();
};

static const auto inferBooleanCompareFunction = [](std::vector<::mlir::Type> const& argTypes,
                                                   TypeInferenceContext& context) -> ::mlir::Type {
  if(hasSymbolicArguments(argTypes)) {
    return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
  }
  return ::mlir::IntegerType::get(1, context.mlirContext);
};

static const auto inferProjectType = [](std::vector<::mlir::Type> const& argTypes,
                                        TypeInferenceContext& context) -> ::mlir::Type {
  auto& symbol = *context.symbolOp;

  // Parse operands: 1. (List ...), 2. TupleStream
  std::vector<::mlir::StringRef> columns;
  auto listCombine = ::mlir::dyn_cast_or_null<::mlir::sexpr::CombineOp>(
      symbol.getOperands().front().getDefiningOp());

  auto streamUnion = symbol.getOperands()[1]
                         .getType()
                         .dyn_cast_or_null<SymbolOrValueType>()
                         .getBaseType()
                         .dyn_cast_or_null<TupleStreamUnionType>();
  if((!listCombine) || (!streamUnion)) {
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

  std::vector<TupleStreamType> newTupleStreams;
  context.openRelations.clear();

  // Filter each child TupleStream's column
  for(auto& stream : streamUnion.getTupleStreams()) {
    std::map<std::string, ::mlir::Type> newFields;
    auto const& oldFields = stream.getFields();

    for(auto const& name : columns) {
      newFields[name.str()] = oldFields.at(name.str());
    }
    context.openRelations.push_back(newFields);
    newTupleStreams.emplace_back(TupleStreamType::get(symbol.getContext(), newFields));
  }

  return SymbolOrValueType::get(symbol.getContext(), sexprtype::SymbolOrValue::VALUE,
                                TupleStreamUnionType::get(symbol.getContext(), newTupleStreams));
};

static const auto inferCollectTuplesType = [](std::vector<::mlir::Type> const& /*argTypes*/,
                                              TypeInferenceContext& context) {
  // Ensures column names from closed relation are no longer in context
  context.openRelations.clear();
  return RelationType::get(context.mlirContext);
};

static const auto inferGetRelationType = [](std::vector<::mlir::Type> const& /*argTypes*/,
                                            TypeInferenceContext& context) -> ::mlir::Type {
  auto& symbol = *context.symbolOp;

  // The first argument must be a string constant!
  auto firstOperand = symbol.getOperands().begin();
  if(firstOperand == symbol.getOperands().end()) {
    // error: no operands
    return ::mlir::NoneType();
  }

  auto stringOp =
      ::mlir::dyn_cast_or_null<::mlir::sexpr::StringConstantOp>((*firstOperand).getDefiningOp());

  if(stringOp == nullptr) {
    // error: Operand is not a string constant
    return SymbolOrValueType::get(symbol.getContext(), sexprtype::SymbolOrValue::SYMBOL, {});
  }

  auto relationName = stringOp.value();
  auto table = context.database->getRelation(std::string(relationName));
  auto rawTablePtr = std::dynamic_pointer_cast<arrow::DenseUnionArray>(table.get());
  auto unionType = rawTablePtr->union_type();

  std::vector<TupleStreamType> streamTypes;
  for(auto const& field : unionType->fields()) {
    std::map<std::string, ::mlir::Type> fieldsAndTypes;
    auto structType = std::dynamic_pointer_cast<arrow::StructType>(field->type());
    for(auto const& columnWithType : structType->fields()) {
      // TODO: take into account what type an expression evaluates to
      fieldsAndTypes[columnWithType->name()] =
          conversion::arrowTypeToMLIRType(symbol.getContext(), columnWithType->type().get());
    }
    streamTypes.emplace_back(TupleStreamType::get(symbol.getContext(), fieldsAndTypes));
    // Ensures column names are in type context
    context.openRelations.push_back(fieldsAndTypes);
  }

  auto openRelationType = TupleStreamUnionType::get(symbol.getContext(), streamTypes);

  return openRelationType;
};

static const auto inferStringJoin = [](std::vector<::mlir::Type> const& arguments,
                                       TypeInferenceContext& context) {
  int totalLength = 0;
  for(auto const& arg : arguments) {
    ::mlir::Type extractedType;
    if(arg.isa<SymbolOrValueType>()) {
      extractedType = arg.dyn_cast<SymbolOrValueType>().getBaseType();
    } else {
      extractedType = arg;
    }
    auto str = extractedType.dyn_cast_or_null<StringType>();
    if(!str) {
      throw std::runtime_error("Expected all arguments to be strings");
    }
    totalLength += str.getLength();
  }

  return StringType::get(context.mlirContext, totalLength);
};

static const auto inferSelectType = [](std::vector<::mlir::Type> const& arguments,
                                       TypeInferenceContext& context) -> ::mlir::Type {
  if(hasSymbolicArguments(arguments)) {
    return SymbolOrValueType::get(context.mlirContext, SymbolOrValue::SYMBOL, {});
  }
  return arguments[1];
};

static const auto inferWhereClauseType = [](std::vector<::mlir::Type> const& arguments,
                                            TypeInferenceContext& context) -> ::mlir::Type {
  std::vector<::mlir::Type> resultTypes;
  std::set<std::string> fieldNames;
  for(auto const& relation : context.openRelations) {
    std::vector<::mlir::Type> inputTypes;

    for(auto const& argumentSymbol : context.argumentSymbols) {
      auto it = relation.find(argumentSymbol);
      if(it == relation.end()) {
        throw std::runtime_error("Expected symbol " + argumentSymbol + " in relation");
      }

      inputTypes.emplace_back(
          SymbolOrValueType::get(context.mlirContext, SymbolOrValue::VALUE, it->second));
      fieldNames.emplace(argumentSymbol);
    }

    // Set function type
    auto returnType = SymbolOrValueType::get(context.mlirContext, SymbolOrValue::VALUE,
                                             ::mlir::IntegerType::get(1, context.mlirContext));
    auto funcType = ::mlir::FunctionType::get(inputTypes, returnType, context.mlirContext);
    resultTypes.emplace_back(funcType);
  }

  // Save the database fields that this Where clause interacts with as an attribute
  std::vector<::mlir::Attribute> fields;
  std::transform(
      fieldNames.begin(), fieldNames.end(), std::back_inserter(fields),
      [&](std::string const& el) { return ::mlir::StringAttr::get(el, context.mlirContext); });

  context.symbolOp->getParentOp()->setAttr("fields", ::mlir::ArrayAttr::get(fields, context.mlirContext));
  return GenericTupleStreamUnionType::get(context.mlirContext, resultTypes);
};

static const auto inferCreateSymbolType = [](std::vector<::mlir::Type> const& arguments,
                                             TypeInferenceContext& context) -> ::mlir::Type {
  // Extract the symbol name
  auto symbolNameDefinition = llvm::dyn_cast_or_null<::mlir::sexpr::StringConstantOp>(
      context.symbolOp->getOperands()[0].getDefiningOp());
  if(!symbolNameDefinition) {
    return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL,
                                  llvm::Optional<::mlir::Type>{});
  }
  auto symbolName = symbolNameDefinition.value().str();

  // Search whether this symbol is referencing a column of the currently open database
  if(!context.openRelations.empty()) {
    std::vector<::mlir::Type> symbolTypes;
    for(auto const& relation : context.openRelations) {
      auto it = relation.find(symbolName);
      if(it != relation.end()) {
        symbolTypes.emplace_back(it->second);
      } else {
        return GenericTupleStreamUnionType::get(context.mlirContext, symbolTypes);
      }
    }

    // Add function argument to context, and store argument index in symbol context
    auto it = std::find(context.argumentSymbols.begin(), context.argumentSymbols.end(), symbolName);
    if(it == context.argumentSymbols.end()) {
      context.symbolOp->setAttr(
          "functionArgPosition",
          ::mlir::IntegerAttr::get(::mlir::IntegerType::get(32, context.mlirContext),
                                   context.argumentSymbols.size()));
      context.argumentSymbols.push_back(symbolName);
    } else {
      context.symbolOp->setAttr(
          "functionArgPosition",
          ::mlir::IntegerAttr::get(::mlir::IntegerType::get(32, context.mlirContext),
                                   std::distance(context.argumentSymbols.begin(), it)));
    }

    return GenericTupleStreamUnionType::get(context.mlirContext, symbolTypes);
  }

  return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL,
                                llvm::Optional<::mlir::Type>{});
};

const std::map<std::string,
               std::function<::mlir::Type(std::vector<::mlir::Type>&, TypeInferenceContext&)>>
    operatorToType{
        {"Plus", inferArithmeticType},         {"Minus", inferArithmeticType},
        {"Mul", inferArithmeticType},          {"Div", inferArithmeticType},
        {"StringJoin", inferStringJoin},       {"Greater", inferBooleanCompareFunction},
        {"Less", inferBooleanCompareFunction}, {"Eq", inferBooleanCompareFunction},
        {"Symbol", inferCreateSymbolType},     {"Project", inferProjectType},
        {"Select", inferSelectType},           {"Where", inferWhereClauseType},
        {"GetRelation", inferGetRelationType}, {"CollectTuples", inferCollectTuplesType}};

bool isRegisteredSymbol(std::string const& name) {
  return operatorToType.find(name) != operatorToType.end();
}

::mlir::Type inferSymbolType(std::string const& symbolName, std::vector<::mlir::Type>& argTypes,
                             TypeInferenceContext& context) {
  auto inferenceFuncIterator = operatorToType.find(symbolName);
  if(inferenceFuncIterator == operatorToType.end()) {
    return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
  }

  auto baseType = (inferenceFuncIterator->second)(argTypes, context);

  if(baseType.isa<SymbolOrValueType>()) {
    return baseType;
  }
  return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::VALUE, baseType);
}

bool hasSymbolicArguments(const std::vector<::mlir::Type>& arguments) {
  for(auto const& argument : arguments) {
    if(argument.isa<SymbolOrValueType>() &&
       argument.dyn_cast<SymbolOrValueType>().isSymbolic() == sexprtype::SymbolOrValue::SYMBOL) {
      return true;
    }
  }
  return false;
}

} // namespace boss::mlir::inference