#include "TypeInference.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/HashTable.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "TypeConversions.hpp"
#include <iostream>
#include <map>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/Types.h>
#include <set>

namespace boss::mlir::inference {

using ::mlir::sexpr::CombineOp;
using ::mlir::sexpr::StringConstantOp;
using ::mlir::sexpr::SymbolOp;
using sexprtype::SymbolOrValue;

int numUnionStreamArgs(std::vector<::mlir::Type> const& arguments);
bool isSymbolic(::mlir::Type type);

::mlir::Type handleTupleStreamUnion(std::function<::mlir::Type(std::vector<::mlir::Type>)> callback,
                                    std::vector<::mlir::Type> const& argTypes,
                                    TypeInferenceContext& context) {
  // Check whether there are union streams as input
  auto numUnions = numUnionStreamArgs(argTypes);
  if(numUnions > 0) {
    std::vector<::mlir::Type> newUnionTypes;
    for(auto i = 0; i < numUnions; i++) {
      std::vector<::mlir::Type> inputTypes;
      for(auto arg : argTypes) {
        auto baseType = arg.dyn_cast<SymbolOrValueType>().getBaseTypeChecked();
        if(baseType.hasValue() && baseType.getValue().isa<GenericTupleStreamUnionType>()) {
          inputTypes.emplace_back(
              baseType.getValue().dyn_cast<GenericTupleStreamUnionType>().getChildren()[i]);
        } else {
          inputTypes.emplace_back(arg);
        }
      }
      auto resultType = callback(inputTypes);
      newUnionTypes.emplace_back(resultType);
    }
    return GenericTupleStreamUnionType::get(context.mlirContext, newUnionTypes);
  }
  return callback(argTypes);
}

// TODO refactor this to its own class or something
std::vector<std::pair<std::string, std::string>> extractLambdaArgs(TypeInferenceContext& context) {
  std::vector<std::pair<std::string, std::string>> args;
  auto const& functionArguments =
      context.symbolOp->getOperand(0).getDefiningOp<CombineOp>().getHead().getOperands();
  for(auto const& argument : functionArguments) {
    // Extract the argument name and type
    auto nameAndTypePair = argument.getDefiningOp<CombineOp>().getHead();
    auto name = nameAndTypePair.getOperand(0).getDefiningOp<StringConstantOp>().value().str();
    auto type = nameAndTypePair.getOperand(1).getDefiningOp<StringConstantOp>().value().str();
    context.argumentSymbols.push_back(name);
    args.push_back({name, type});
  }
  return args;
}

static const auto inferArithmeticType = [](std::vector<::mlir::Type> const& argTypes,
                                           TypeInferenceContext& context) -> ::mlir::Type {
  if(hasSymbolicArguments(argTypes)) {
    return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
  }

  // TODO fix type inference
  return ::mlir::IntegerType::get(32, context.mlirContext);
};

static const auto inferBooleanCompareFunction = [](std::vector<::mlir::Type> const& argTypes,
                                                   TypeInferenceContext& context) -> ::mlir::Type {
  auto handleInference = [&](auto inputTypes) -> ::mlir::Type {
    if(hasSymbolicArguments(inputTypes)) {
      return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
    }
    return ::mlir::IntegerType::get(1, context.mlirContext);
  };

  return handleTupleStreamUnion(handleInference, argTypes, context);
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
  context.activePartitions.clear();

  // Filter each child TupleStream's column
  for(auto& stream : streamUnion.getTupleStreams()) {
    std::map<std::string, ::mlir::Type> newFields;
    auto const& oldFields = stream.getFields();

    for(auto const& name : columns) {
      newFields[name.str()] = oldFields.at(name.str());
    }
    context.activePartitions.push_back(newFields);
    newTupleStreams.emplace_back(TupleStreamType::get(symbol.getContext(), newFields));
  }

  return SymbolOrValueType::get(symbol.getContext(), sexprtype::SymbolOrValue::VALUE,
                                TupleStreamUnionType::get(symbol.getContext(), newTupleStreams));
};

static const auto inferCollectTuplesType = [](std::vector<::mlir::Type> const& /*argTypes*/,
                                              TypeInferenceContext& context) {
  // Ensures column names from closed relation are no longer in context
  context.activePartitions.clear();
  return RelationType::get(context.mlirContext);
};

static const auto inferGroupbyType = [](std::vector<::mlir::Type> const& /*argTypes*/,
                                        TypeInferenceContext& context) {
  context.activePartitions.clear();
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
  for(auto i = 0; i < unionType->num_fields(); i++) {
    std::map<std::string, ::mlir::Type> fieldsAndTypes;
    auto structType = std::dynamic_pointer_cast<arrow::StructType>(unionType->field(i)->type());
    auto structArray = std::dynamic_pointer_cast<arrow::StructArray>(rawTablePtr->field(i));
    for(auto const& columnWithType : structType->fields()) {
      // TODO: take into account what type an expression evaluates to
      if (context.symbolTable.find(columnWithType->name()) != context.symbolTable.end()) {
        // Check if its in the symbol table
        fieldsAndTypes[columnWithType->name()] = context.symbolTable[columnWithType->name()];
      } else {
        // Else just convert the arrow type
        context.currentArray = structArray->GetFieldByName(columnWithType->name());
        fieldsAndTypes[columnWithType->name()] =
            conversion::arrowTypeToMLIRType(context, columnWithType->type().get());
      }
    }
    streamTypes.emplace_back(TupleStreamType::get(symbol.getContext(), fieldsAndTypes));
    // Ensures column names are in type context
    context.activePartitions.push_back(fieldsAndTypes);
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

static const auto inferLambdaType = [](std::vector<::mlir::Type> const& arguments,
                                       TypeInferenceContext& context) -> ::mlir::Type {
  std::vector<::mlir::Type> resultTypes;
  std::set<std::string> fieldNames;

  // Create a new function type for each active partition (each stream in the union)
  for(auto const& relation : context.activePartitions) {
    std::vector<::mlir::Type> inputTypes;

    for(auto const& argumentSymbol : context.argumentSymbols) {
      // Check whether this symbol was a normal input symbol
      auto symbolIt = context.symbolTable.find(argumentSymbol);
      if(symbolIt != context.symbolTable.end()) {
        // Add this type to the inputs for the function
        inputTypes.emplace_back(
            SymbolOrValueType::get(context.mlirContext, SymbolOrValue::VALUE, symbolIt->second));
        fieldNames.emplace(argumentSymbol);
        continue;
      }
      // Check whether this symbol was a database symbol
      auto it = relation.find(argumentSymbol);
      if(it != relation.end()) {
        // Add this type to the inputs for the function
        inputTypes.emplace_back(
            SymbolOrValueType::get(context.mlirContext, SymbolOrValue::VALUE, it->second));
        fieldNames.emplace(argumentSymbol);
        continue;
      }
      throw std::runtime_error("The field was neither in the database nor in the symbol table");
    }

    // Set function type
    auto returnType = context.symbolOp->getOperand(1).getType();
    auto funcType = ::mlir::FunctionType::get(inputTypes, returnType, context.mlirContext);
    resultTypes.emplace_back(funcType);
  }

  // reset context
  context.argumentSymbols = {};
  for(auto const& [arg, _] : extractLambdaArgs(context)) {
    context.symbolTable.erase(context.symbolTable.find(arg));
  }

  // Save the database fields that this Where clause interacts with as an attribute
  std::vector<::mlir::Attribute> fields;
  std::transform(
      fieldNames.begin(), fieldNames.end(), std::back_inserter(fields),
      [&](std::string const& el) { return ::mlir::StringAttr::get(el, context.mlirContext); });

  context.symbolOp->getParentOp()->setAttr("fields",
                                           ::mlir::ArrayAttr::get(fields, context.mlirContext));
  return GenericTupleStreamUnionType::get(context.mlirContext, resultTypes);
};

static const auto inferWhereClauseType = [](std::vector<::mlir::Type> const& arguments,
                                            TypeInferenceContext& context) -> ::mlir::Type {
  std::vector<::mlir::Type> resultTypes;
  std::set<std::string> fieldNames;
  auto i = 0;
  for(auto const& relation : context.activePartitions) {
    std::vector<::mlir::Type> inputTypes;

    for(auto const& argumentSymbol : context.argumentSymbols) {
      auto it = relation.find(argumentSymbol);
      if(it == relation.end()) {
        throw std::runtime_error("Expected symbol " + argumentSymbol + " in relation");
      }

      if(it->second.isa<SymbolOrValueType>()) {
        inputTypes.emplace_back(it->second);
      } else {
        inputTypes.emplace_back(
            SymbolOrValueType::get(context.mlirContext, SymbolOrValue::VALUE, it->second));
      }
      fieldNames.emplace(argumentSymbol);
    }

    // Set function type
    auto returnBase = arguments[0]
                          .dyn_cast<SymbolOrValueType>()
                          .getBaseType()
                          .dyn_cast_or_null<GenericTupleStreamUnionType>()
                          .getChildren()[i++];
    ::mlir::Type returnType;
    if (returnBase.isa<SymbolOrValueType>()) {
      returnType = returnBase;
    } else {
      returnType = SymbolOrValueType::get(context.mlirContext, SymbolOrValue::VALUE, returnBase);
    }

    auto funcType = ::mlir::FunctionType::get(inputTypes, returnType, context.mlirContext);
    resultTypes.emplace_back(funcType);
  }

  // reset context
  context.argumentSymbols = {};

  // Save the database fields that this Where clause interacts with as an attribute
  std::vector<::mlir::Attribute> fields;
  std::transform(
      fieldNames.begin(), fieldNames.end(), std::back_inserter(fields),
      [&](std::string const& el) { return ::mlir::StringAttr::get(el, context.mlirContext); });

  context.symbolOp->getParentOp()->setAttr("fields",
                                           ::mlir::ArrayAttr::get(fields, context.mlirContext));
  return GenericTupleStreamUnionType::get(context.mlirContext, resultTypes);
};

::mlir::Type getSymbolTableType(std::string const& symbolName, TypeInferenceContext& context) {
  auto symbolTableIt = context.symbolTable.find(symbolName);
  if(symbolTableIt != context.symbolTable.end()) {
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
    return symbolTableIt->second;
  }

  // Search whether this symbol is referencing a column in the partitions
  if(!context.activePartitions.empty()) {
    std::vector<::mlir::Type> symbolTypes;
    for(auto const& relation : context.activePartitions) {
      auto it = relation.find(symbolName);
      if(it != relation.end()) {
        symbolTypes.emplace_back(it->second);
      } else {
        return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL,
                                      llvm::Optional<::mlir::Type>{});
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
}

static const auto inferJoinType = [](std::vector<::mlir::Type> const& arguments,
                                     TypeInferenceContext& context) -> ::mlir::Type {
  auto leftBaseType =
      context.symbolOp->getOperand(1).getType().dyn_cast_or_null<SymbolOrValueType>().getBaseType();
  auto leftTupleStreamUnion = leftBaseType.dyn_cast_or_null<TupleStreamUnionType>();

  auto hashTablePtr =
      context.symbolOp->getOperand(2).getDefiningOp<::mlir::sexpr::IntegerConstantOp>().value();
  auto hashTable = reinterpret_cast<runtime::hash::HashTable*>(hashTablePtr);

  // todo handle context open relations... Actually maybe not, handled by interpreter

  if(!leftTupleStreamUnion || (hashTable == nullptr)) {
    throw std::runtime_error("Error: Expecting tuple stream union and hash table");
  }

  std::vector<TupleStreamType> outputTupleStreams;
  for(auto const& leftTupleStream : leftTupleStreamUnion.getTupleStreams()) {
    for(auto i = 0UL; i < hashTable->getNumChildArrays(); i++) {
      auto rightFields = hashTable->getChildFields(i);

      std::map<std::string, ::mlir::Type> newFields;

      for(auto const& leftField : leftTupleStream.getFields()) {
        newFields[leftField.first] = leftField.second;
      }

      for(auto const& rightField : rightFields) {
        newFields[rightField.first] =
            boss::mlir::conversion::arrowTypeToMLIRType(context, rightField.second);
      }

      // TODO only if the types match on the joining fields
      auto combinedTupleStream = TupleStreamType::get(context.mlirContext, newFields);
      outputTupleStreams.emplace_back(combinedTupleStream);
    }
  }

  auto outputTupleStream = TupleStreamUnionType::get(context.mlirContext, outputTupleStreams);
  return outputTupleStream;
};

static const auto inferBuildHashTable = [](std::vector<::mlir::Type> const& arguments,
                                           TypeInferenceContext& context) -> ::mlir::Type {
  return ::mlir::IndexType::get(context.mlirContext);
};

static const auto inferBooleanType = [](std::vector<::mlir::Type> const& arguments,
                                        TypeInferenceContext& context) -> ::mlir::Type {
  if(hasSymbolicArguments(arguments)) {
    return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::SYMBOL, {});
  }
  return ::mlir::IntegerType::get(1, context.mlirContext);
};

static const auto inferNextValueType = [](std::vector<::mlir::Type> const& arguments,
                                          TypeInferenceContext& context) -> ::mlir::Type {
  // The return type is passed in as argument 2
  const_cast<::mlir::Type*>(&arguments[2])->dump();
  return arguments[2];
};

const std::map<std::string,
               std::function<::mlir::Type(std::vector<::mlir::Type> const&, TypeInferenceContext&)>>
    operatorToType{{"Plus", inferArithmeticType},
                   {"Minus", inferArithmeticType},
                   {"Mul", inferArithmeticType},
                   {"Div", inferArithmeticType},
                   {"StringJoin", inferStringJoin},
                   {"Greater", inferBooleanCompareFunction},
                   {"BuildHashTable", inferBuildHashTable},
                   {"And", inferBooleanType},
                   {"Less", inferBooleanCompareFunction},
                   {"Eq", inferBooleanCompareFunction},
                   {"Project", inferProjectType},
                   {"Select", inferSelectType},
                   {"Where", inferWhereClauseType},
                   {"Lambda", inferLambdaType},
                   {"GetRelation", inferGetRelationType},
                   {"CollectTuples", inferCollectTuplesType},
                   {"NextValue", inferNextValueType},
                   {"Join", inferJoinType},
                   {"GroupBy", inferGroupbyType}};

const std::map<std::string, std::function<void(::mlir::sexpr::SymbolOp* op, TypeInferenceContext&)>>
    contextUpdateMap{{"Lambda", [](::mlir::sexpr::SymbolOp* op, TypeInferenceContext& context) {
                        // Extract lambda arguments
                        auto args = extractLambdaArgs(context);
                        for(auto const& [name, type] : args) {
                          context.symbolTable[name] =
                              boss::mlir::conversion::stringToMLIRType(context.mlirContext, type);
                        }
                      }}};

bool isRegisteredSymbol(std::string const& name) {
  return operatorToType.find(name) != operatorToType.end();
}

::mlir::Type inferSymbolType(std::string const& symbolName,
                             const std::vector<::mlir::Type>& argTypes,
                             TypeInferenceContext& context) {
  ::mlir::Type baseType;
  auto inferenceFuncIterator = operatorToType.find(symbolName);

  if(inferenceFuncIterator == operatorToType.end()) {
    // Check whether it is a built-in function
    // If not, check whether this symbol has been defined in the symbol table
    baseType = getSymbolTableType(symbolName, context);
  } else {
    // If yes, call the correct inference function
    // TODO the arg types are wrong here because we recursed at a different point in time
    baseType = (inferenceFuncIterator->second)(argTypes, context);
  }

  // Make sure we don't get recursive SymbolOrValue types
  if(baseType.isa<SymbolOrValueType>()) {
    return baseType;
  }
  return SymbolOrValueType::get(context.mlirContext, sexprtype::SymbolOrValue::VALUE, baseType);
}

bool isSymbolic(::mlir::Type type) {
  return type.isa<SymbolOrValueType>() &&
         (type.dyn_cast<SymbolOrValueType>().isSymbolic() == sexprtype::SymbolOrValue::SYMBOL);
}

int numUnionStreamArgs(std::vector<::mlir::Type> const& arguments) {
  for(auto& argument : arguments) {
    if(!argument.isa<SymbolOrValueType>()) {
      continue;
    }
    auto base = argument.dyn_cast<SymbolOrValueType>().getBaseTypeChecked();

    if(!base.hasValue()) {
      continue;
    }

    if(base.getValue().isa<GenericTupleStreamUnionType>()) {
      return base.getValue().dyn_cast<GenericTupleStreamUnionType>().getChildren().size();
    }
  }
  return 0;
}

bool hasSymbolicArguments(std::vector<::mlir::Type> const& arguments) {
  // TODO propagate generictuplestream properly
  for(auto& argument : arguments) {
    if(isSymbolic(argument)) {
      return true;
    }
  }
  return false;
}

void updateContext(::mlir::sexpr::SymbolOp* symbolOp, TypeInferenceContext& context) {
  auto contextUpdater = contextUpdateMap.find(symbolOp->name().str());
  if(contextUpdater != contextUpdateMap.end()) {
    contextUpdater->second(symbolOp, context);
  }
}

} // namespace boss::mlir::inference