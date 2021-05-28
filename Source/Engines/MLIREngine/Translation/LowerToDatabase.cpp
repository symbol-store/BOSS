#include "Engines/MLIREngine.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseDialect.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryDialect.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/HashAggregate.hpp"
#include "Engines/MLIREngine/Runtime/HashTable.hpp"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include "Engines/MLIREngine/Types/TypeInference.hpp"
#include "Expression.hpp"
#include "LowerDatabase.hpp"
#include "Utilities.hpp"
#include <iostream>
#include <mlir/Conversion/SCFToStandard/SCFToStandard.h>
#include <mlir/Conversion/StandardToLLVM/ConvertStandardToLLVM.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/SCF/SCF.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mutex>
#include <variant>

namespace {
using namespace mlir;
using boss::mlir::inference::TypeInferenceContext;
using boss::utilities::operator""_;

std::mutex printMutex;

struct DatabaseLoweringPass : public PassWrapper<DatabaseLoweringPass, OperationPass<ModuleOp>> {

  DatabaseLoweringPass(new_runtime::Database& database,
                       std::unordered_map<std::string, boss::Expression> symbolTable)
      : database(database), symbolTable(std::move(symbolTable)){};

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }
  void runOnOperation() final;

  new_runtime::Database& database;
  std::unordered_map<std::string, boss::Expression> symbolTable;
};

struct FuncOpSignatureConversion : public OpConversionPattern<mlir::FuncOp> {
  FuncOpSignatureConversion(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(FuncOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    // Convert signature of function to respect new type conversions caused by database lowering
    auto type = op.getType();

    SmallVector<Type, 1> convertedResults;
    if(failed(converter.convertTypes(type.getResults(), convertedResults))) {
      return failure();
    }

    SmallVector<Type, 1> convertedInputs;
    if(failed(converter.convertTypes(type.getInputs(), convertedInputs))) {
      return failure();
    }

    rewriter.updateRootInPlace(
        op, [&]() { op.setType(rewriter.getFunctionType(convertedInputs, convertedResults)); });

    return success();
  }

  TypeConverter& converter;
};

Value collectTuplesToRelation(ConversionPatternRewriter& rewriter, Value streamValue,
                              new_runtime::RelationBuilder* relationBuilder, Location loc) {
  auto tupleStreamType = streamValue.getType().dyn_cast_or_null<TupleStreamType>();
  if(!tupleStreamType) {
    return {};
  }

  auto savedInsertionPoint = rewriter.saveInsertionPoint();
  rewriter.setInsertionPointAfter(streamValue.getDefiningOp());

  auto runtimeFields =
      boss::mlir::conversion::mlirFieldsToRuntimeFields(tupleStreamType.getFields());

  auto advanceOp = rewriter.create<database::AdvanceBuilderOp>(
      loc,
      reinterpret_cast<size_t>(
          (relationBuilder->getOrCreateTypedStructBuilder(runtimeFields)).get()),
      reinterpret_cast<size_t>((relationBuilder->rawBuilder()).get()),
      relationBuilder->getOrCreateTypedStructBuilderIndex(runtimeFields));

  // Iterate over all values in the tuple stream, and append to builder
  for(auto const& [name, type] : tupleStreamType.getFields()) {
    auto runtimeType = boss::mlir::conversion::mlirTypeToRuntimeType(type, false);
    auto extractOp =
        rewriter.create<database::ExtractFieldFromTupleOp>(loc, streamValue, name, type);
    auto columnBuilder = relationBuilder->getOrCreateColumnBuilder(name, runtimeFields);

    rewriter.create<database::AppendToRelationOp>(
        loc, extractOp.getResult(), reinterpret_cast<size_t>(columnBuilder.get()), runtimeType);
  }

  rewriter.restoreInsertionPoint(savedInsertionPoint);

  return advanceOp.getResult();
}

struct CollectTuplesOpLowering : public OpConversionPattern<database::CollectTuplesOp> {
  CollectTuplesOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(database::CollectTuplesOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto loc = op.getLoc();

    // TODO free somewhere
    auto* relationBuilder = new new_runtime::RelationBuilder;

    for(auto const& operand : operands) {
      auto insertedValueOffset = collectTuplesToRelation(rewriter, operand, relationBuilder, loc);
    }

    auto newOp = rewriter.create<database::FinalizeBuilderOp>(
        loc, reinterpret_cast<size_t>(relationBuilder), "finalizeRelationBuilder");

    rewriter.replaceOp(op, newOp.getResult());
    return success();
  }

  TypeConverter& converter;
};

// Generates code to load the type
struct ArrayLoaderTypeVisitor : arrow::TypeVisitor {
  ::mlir::Value result;

  ArrayLoaderTypeVisitor(std::shared_ptr<arrow::Array> baseArray, mlir::Value offset,
                         ConversionPatternRewriter& rewriter, mlir::Location loc,
                         new_runtime::Database& database,
                         std::unordered_map<std::string, boss::Expression> const& symbolTable)
      : baseArray(std::move(baseArray)), offset(offset), rewriter(rewriter), loc(loc),
        database(database), symbolTable(symbolTable) {}

  std::shared_ptr<arrow::Array> baseArray;
  mlir::Value offset;
  mlir::ConversionPatternRewriter& rewriter;
  mlir::Location loc;
  new_runtime::Database& database;
  std::unordered_map<std::string, boss::Expression> const& symbolTable;
  ::mlir::Value globalSymbolOffset;

  // TODO remaining types
  arrow::Status Visit(const arrow::Int32Type& /*type*/) override {
    auto integerArray = std::dynamic_pointer_cast<arrow::Int32Array>(baseArray);
    auto loadOp = rewriter.create<memory::LoadConstantAddressOp>(
        loc, reinterpret_cast<size_t>(integerArray->raw_values()), rewriter.getI32Type(), offset);
    result = loadOp.getResult();
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::Int64Type& /*type*/) override {
    auto integerArray = std::dynamic_pointer_cast<arrow::Int64Array>(baseArray);
    auto loadOp = rewriter.create<memory::LoadConstantAddressOp>(
        loc, reinterpret_cast<size_t>(integerArray->raw_values()), rewriter.getIndexType(), offset);
    result = loadOp.getResult();
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::FloatType& /*type*/) override {
    auto typedArray = std::dynamic_pointer_cast<arrow::FloatArray>(baseArray);
    auto loadOp = rewriter.create<memory::LoadConstantAddressOp>(
        loc, reinterpret_cast<size_t>(typedArray->raw_values()), rewriter.getF32Type(), offset);
    result = loadOp.getResult();
    return arrow::Status::OK();
  }

  // Visit complex expression
  arrow::Status Visit(const arrow::StructType& type) override {
    auto symbolName = type.field(0)->name();

    if(symbolName == "Symbol") {
      // This complex expression is just a single symbol
      return handleSymbol();
    }

    auto structArray = std::dynamic_pointer_cast<arrow::StructArray>(baseArray);

    std::vector<Value> childResults;
    std::vector<::mlir::Type> operandTypes;
    for(auto i = 1; i < type.num_fields(); i++) {
      auto const& field = type.field(i);

      ArrayLoaderTypeVisitor childVisitor(structArray->field(i), offset, rewriter, loc, database,
                                          symbolTable);
      auto status = field->type()->Accept(&childVisitor);
      if(!status.ok()) {
        return status;
      }

      childResults.emplace_back(childVisitor.result);
      operandTypes.emplace_back(childVisitor.result.getType());
    }

    TypeInferenceContext typeContext(rewriter.getContext(), &database, {}, nullptr);
    auto newType = boss::mlir::inference::inferSymbolType(symbolName, operandTypes, typeContext);

    auto symbolOp = rewriter.create<sexpr::SymbolOp>(
        loc, newType, StringAttr::get(symbolName, rewriter.getContext()), childResults);
    globalSymbolOffset = symbolOp.getResult();

    result = symbolOp.getResult();
    return arrow::Status::OK();
  }

  mlir::Value expressionToValue(boss::Expression const& e, MLIRContext* context) {
    mlir::Value returnValue;
    std::visit(boss::utilities::overload(
        [&](int a) {
          auto intOp = rewriter.create<ConstantIntOp>(loc, a, 32);
          returnValue = intOp.getResult();
        },
        [&](float a) {
          auto floatOp = rewriter.create<ConstantFloatOp>(loc, APFloat(a),
                                                          FloatType::getF32(context));
          returnValue = floatOp.getResult();
        },
        [&](bool) {
          throw std::runtime_error("Not implemented: Type bool");
        },
        [&](size_t) {
          throw std::runtime_error("Not implemented: Type size_t");
        },
        [&](char const*) {
          throw std::runtime_error("Not implemented: Type string");
        },
        [&](std::string) {
          throw std::runtime_error("Not implemented: Type string");
        },
        [&](boss::Symbol) {
          throw std::runtime_error("Not implemented: Type symbol");
        },
        [&](boss::ComplexExpression ce) {
          auto symbolName = ce.getHead().getName();

          std::vector<mlir::Value> childResults;
          std::vector<mlir::Type> operandTypes;
          for (auto const& arg : ce.getArguments()) {
            auto childValue = expressionToValue(arg, context);
            childResults.emplace_back(childValue);
            operandTypes.emplace_back(childValue.getType());
          }

          if (symbolName == "NextValue") {
            childResults.push_back(globalSymbolOffset);
            operandTypes.push_back(rewriter.getIndexType());
          }

          TypeInferenceContext typeContext(rewriter.getContext(), &database, {}, nullptr);
          auto newType = boss::mlir::inference::inferSymbolType(symbolName, operandTypes, typeContext);

          newType.dump();
          for (auto& result : childResults) {
            result.dump();
          }

          auto symbolOp = rewriter.create<sexpr::SymbolOp>(
              loc, newType, StringAttr::get(symbolName, rewriter.getContext()), childResults);

          returnValue = symbolOp.getResult();
        }), e);
    return returnValue;
  }

  arrow::Status handleSymbol() {
    auto* context = rewriter.getContext();
    auto structArray = std::dynamic_pointer_cast<arrow::StructArray>(baseArray);

    auto dictArray = std::dynamic_pointer_cast<arrow::DictionaryArray>(structArray->field(0));
    auto symbolArray = std::dynamic_pointer_cast<arrow::StringArray>(dictArray->dictionary());
    auto symbol = symbolArray->GetString(0);

    // Look up the symbol in the symbol table
    auto it = symbolTable.find(symbol);
    if(it == symbolTable.end()) {
      return arrow::Status::NotImplemented("Not implemented: Undefined symbol " + symbol);
    }

    // Load the symbol offset
    auto globalOffsetArray = std::dynamic_pointer_cast<arrow::Int64Array>(structArray->field(1));
    auto rawOffsetArray = reinterpret_cast<size_t>(globalOffsetArray->raw_values());
    auto globalOffset = rewriter.create<memory::LoadConstantAddressOp>(loc, rawOffsetArray, rewriter.getIndexType(), offset);
    globalSymbolOffset = globalOffset.getResult();

    // Generate code for symbol
    try {
      result = expressionToValue(it->second, context);
    } catch (std::runtime_error const& e) {
      return arrow::Status::NotImplemented(e.what());
    }
    return arrow::Status::OK();
  }
};

struct NextValueOpLowering : public OpConversionPattern<database::NextValueOp> {
  NextValueOpLowering(MLIRContext* ctx, new_runtime::Database& database)
      : OpConversionPattern(ctx), database(database) {}

  LogicalResult matchAndRewrite(database::NextValueOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto const& relationName = op.relationName().str();
    auto relation = database.getRelation(relationName);
    auto* rawTypeCodes = relation.get()->raw_type_codes();
    auto* rawValueOffsets = relation.get()->raw_value_offsets();

    // The actual offset to load in the database
    auto offsetIndex =
        rewriter.create<mlir::IndexCastOp>(op.getLoc(), operands[0], rewriter.getIndexType());
    auto nextValueIndex =
        rewriter.create<mlir::AddIOp>(op.getLoc(), offsetIndex.getResult(), operands[1]);

    // Load the type id and the offset into the child array where the actual data is stored
    auto typeId = rewriter.create<memory::LoadConstantAddressOp>(
        op.getLoc(), reinterpret_cast<size_t>(rawTypeCodes),
        IntegerType::get(8, rewriter.getContext()), nextValueIndex.getResult());
    auto valueOffset = rewriter.create<memory::LoadConstantAddressOp>(
        op.getLoc(), reinterpret_cast<size_t>(rawValueOffsets),
        IntegerType::get(32, rewriter.getContext()), nextValueIndex.getResult());

    auto typeIdIndex =
        rewriter.create<mlir::IndexCastOp>(op.getLoc(), typeId.getResult(), rewriter.getIndexType());
    auto valueOffsetIndex =
        rewriter.create<mlir::IndexCastOp>(op.getLoc(), valueOffset.getResult(), rewriter.getIndexType());

    auto columnId = 0;
    auto relationFields = std::dynamic_pointer_cast<arrow::StructArray>(relation.get()->field(0))
                              ->struct_type()
                              ->fields();
    for(auto it = relationFields.begin(); it != relationFields.end(); it++) {
      if(it->get()->name() == op.fieldName().str()) {
        columnId = std::distance(relationFields.begin(), it);
      }
    }

    auto relationAttr = rewriter.getIndexAttr(reinterpret_cast<size_t>(relation.get().get()));
    auto columnIdAttr = rewriter.getIndexAttr(columnId);

    auto loadOp = rewriter.create<database::LoadArrayIndirectOp>(op.getLoc(), op.getType(), relationAttr,
                                                   columnIdAttr, typeIdIndex.getResult(),
                                                   valueOffsetIndex.getResult());

    rewriter.replaceOp(op, loadOp.getResult());
    return success();
  }

  new_runtime::Database& database;
};

struct GetRelationOpLowering : public OpConversionPattern<database::GetRelationOp> {
  GetRelationOpLowering(MLIRContext* ctx, new_runtime::Database& database,
                        std::unordered_map<std::string, boss::Expression> const& symbolTable)
      : OpConversionPattern(ctx), database(database), symbolTable(symbolTable) {}

  LogicalResult matchAndRewrite(database::GetRelationOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    auto loc = op.getLoc();

    auto resultTupleStream = rewriter.getType<TupleStreamType>(op.getTupleStream().getFields());

    auto unionChildArray =
        database.getRelation(op.relationName().str()).get()->field(op.unionChildIndex());
    auto relationLength = unionChildArray->length();
    auto relation = std::dynamic_pointer_cast<arrow::StructArray>(unionChildArray);

    // Create loop
    auto lowerBound = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), 0);
    auto upperBound = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), relationLength);
    auto step = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), 1);
    auto loop = rewriter.create<scf::ForOp>(rewriter.getUnknownLoc(), lowerBound, upperBound, step);
    PatternRewriter::InsertionGuard insertionGuard(rewriter);
    rewriter.setInsertionPointToStart(loop.getBody());

    std::vector<::mlir::Value> loadedValues;
    for(auto const& field : relation->fields()) {
      // Different loading code based on type
      ArrayLoaderTypeVisitor visitor(field, loop.getInductionVar(), rewriter, loc, database,
                                     symbolTable);
      auto status = field->type()->Accept(&visitor);
      if(!status.ok()) {
        throw std::runtime_error(status.message());
      }
      loadedValues.push_back(visitor.result);
    }

    rewriter.replaceOpWithNewOp<database::PackFieldsIntoTupleOp>(op, loadedValues,
                                                                 resultTupleStream);

    return success();
  }

  new_runtime::Database& database;
  std::unordered_map<std::string, boss::Expression> const& symbolTable;
};

struct ProjectionOpLowering : public OpConversionPattern<database::ProjectionOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::ProjectionOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    mlir::SmallVector<Value, 4> newValues;
    auto outputStream = op.getType().cast<TupleStreamType>();

    auto savedPoint = rewriter.saveInsertionPoint();
    rewriter.setInsertionPointAfter(operands[0].getDefiningOp());

    for(auto const& [name, type] : outputStream.getFields()) {
      auto extractedVal = rewriter.create<database::ExtractFieldFromTupleOp>(
          op.getLoc(), op.getOperand(), name, type);
      newValues.push_back(extractedVal.getResult());
    }

    auto tupleStreamVal =
        rewriter.create<database::PackFieldsIntoTupleOp>(op.getLoc(), newValues, outputStream);
    rewriter.replaceOp(op, tupleStreamVal.getResult());
    rewriter.restoreInsertionPoint(savedPoint);
    return success();
  }
};

struct LookupJoinOpLowering : public OpConversionPattern<database::LookupJoinOp> {
  LookupJoinOpLowering(MLIRContext* ctx, new_runtime::Database& database)
      : OpConversionPattern(ctx), database(database) {}

  LogicalResult matchAndRewrite(database::LookupJoinOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto savedInsertionPoint = rewriter.saveInsertionPoint();
    rewriter.setInsertionPointAfter(operands[0].getDefiningOp());

    auto* hashTable = reinterpret_cast<runtime::hash::HashTable*>(op.table());

    auto inputTupleStream = op.getOperand().getType().dyn_cast_or_null<TupleStreamType>();
    auto const& inputFields = inputTupleStream.getFields();
    auto outputTupleStream = op.getType().dyn_cast_or_null<TupleStreamType>();

    auto* map = hashTable->getChildIndexMap(op.rightTupleStreamIndex());
    auto* storageRelation = hashTable->getChildArray(op.rightTupleStreamIndex());

    // hash the fields that are relevant to the join
    std::vector<Value> valuesToHash{};
    for(auto const& fieldAttr : op.leftFields()) {
      auto fieldName = fieldAttr.dyn_cast_or_null<StringAttr>().getValue().str();
      auto fieldType = inputFields.find(fieldName)->second;
      auto extractedVal = rewriter.create<database::ExtractFieldFromTupleOp>(
          op.getLoc(), op.getOperand(), fieldName, fieldType);
      valuesToHash.emplace_back(extractedVal.getResult());
    }
    auto hashedValues =
        rewriter.create<database::HashValuesOp>(op.getLoc(), rewriter.getIndexType(), valuesToHash);

    // TODO this should return a list and then you have to look up all the values
    // Look up the value in the hash table
    auto hashTableResult = rewriter.create<database::FindInHashTableOp>(
        op.getLoc(), rewriter.getIndexType(), hashedValues.getResult(),
        rewriter.getI64IntegerAttr(reinterpret_cast<size_t>(map)));

    // Check whether it was found (returns -1 if not)
    auto negativeConstant = rewriter.create<ConstantIndexOp>(op.getLoc(), -1);
    auto eqOp = rewriter.create<::mlir::CmpIOp>(
        op.getLoc(), CmpIPredicate::ne, negativeConstant.getResult(), hashTableResult.getResult());
    auto checkNotNegativeOp = rewriter.create<scf::IfOp>(op.getLoc(), eqOp.result(), false);
    rewriter.setInsertionPointToStart(&checkNotNegativeOp.thenRegion().front());

    auto* relation = hashTable->getChildArray(op.rightTupleStreamIndex());
    auto structArray = dynamic_cast<arrow::StructArray*>(relation);

    // Check that the values really match
    std::vector<Value> comparisonResults;
    std::vector<Type> comparisonTypes;
    for(auto i = 0U; i < op.leftFields().size(); i++) {
      auto leftFieldName = op.leftFields()[i].dyn_cast_or_null<StringAttr>().getValue().str();
      auto rightFieldName = op.rightFields()[i].dyn_cast_or_null<StringAttr>().getValue().str();
      auto leftFieldType = outputTupleStream.getFields().find(leftFieldName)->second;
      auto rightFieldType = outputTupleStream.getFields().find(rightFieldName)->second;

      auto leftValue = rewriter.create<database::ExtractFieldFromTupleOp>(
          op.getLoc(), operands[0], leftFieldName, leftFieldType);

      // Find the correct location
      auto relationFields = hashTable->getChildFields(op.rightTupleStreamIndex());
      auto rightFieldPosition =
          std::distance(relationFields.begin(), relationFields.find(rightFieldName));
      auto rightArray = structArray->field(rightFieldPosition);

      ArrayLoaderTypeVisitor visitor(rightArray, hashTableResult.getResult(), rewriter, op.getLoc(),
                                     database, {});
      rightArray->type()->Accept(&visitor);
      auto rightValue = visitor.result;

      // Compare these values
      TypeInferenceContext typeContext{op.getContext(), &database, {}, nullptr};
      auto type = boss::mlir::inference::inferSymbolType(
          "Eq", {leftValue.getType(), rightValue.getType()}, typeContext);
      auto compareOp = rewriter.create<sexpr::SymbolOp>(op.getLoc(), type,
                                                        StringAttr::get("Eq", op.getContext()),
                                                        ValueRange{leftValue, rightValue});
      comparisonResults.emplace_back(compareOp.getResult());
      comparisonTypes.emplace_back(compareOp.getResult().getType());
    }

    TypeInferenceContext typeContext{op.getContext(), &database, {}, nullptr};
    auto type = boss::mlir::inference::inferSymbolType("And", comparisonTypes, typeContext);

    auto wasMatchOp = rewriter.create<sexpr::SymbolOp>(
        op.getLoc(), type, StringAttr::get("And", op.getContext()), comparisonResults);

    // If it was a match, load the remaining values and output the tuple
    auto wasMatchCond = rewriter.create<scf::IfOp>(op.getLoc(), wasMatchOp.getResult(), false);
    rewriter.setInsertionPointToStart(&wasMatchCond.thenRegion().front());

    // TODO optimise with other loop (don't load twice)
    std::vector<Value> loadedTupleValues;
    // Load left values
    for(auto const& [name, type] : inputTupleStream.getFields()) {
      auto extractOp =
          rewriter.create<database::ExtractFieldFromTupleOp>(op.getLoc(), operands[0], name, type);
      loadedTupleValues.emplace_back(extractOp.getResult());
    }

    // Load right values
    for(auto const& field : structArray->fields()) {
      ArrayLoaderTypeVisitor visitor(field, hashTableResult.getResult(), rewriter, op.getLoc(),
                                     database, {});
      field->type()->Accept(&visitor);
      loadedTupleValues.emplace_back(visitor.result);
    }

    rewriter.replaceOpWithNewOp<database::PackFieldsIntoTupleOp>(op, loadedTupleValues,
                                                                 outputTupleStream);

    rewriter.restoreInsertionPoint(savedInsertionPoint);
    return success();
  }

  new_runtime::Database& database;
};

struct BuildJoinOpLowering : public OpConversionPattern<database::BuildJoinTableOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::BuildJoinTableOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto* hashTable = reinterpret_cast<runtime::hash::HashTable*>(op.table());
    auto hashBuilder = hashTable->getBuilder();

    // Process each tuple stream from the right relation
    auto tupleStreamIndex = 0U;
    for(auto const& tupleStreamOp : operands) {
      auto savedInsertionPoint = rewriter.saveInsertionPoint();
      rewriter.setInsertionPointAfter(tupleStreamOp.getDefiningOp());

      // Insert the values into hash map storage relation
      auto insertedLocation =
          collectTuplesToRelation(rewriter, tupleStreamOp, &hashBuilder, op.getLoc());

      auto inputTupleStream = tupleStreamOp.getType().dyn_cast_or_null<TupleStreamType>();
      auto const& fields = inputTupleStream.getFields();

      // get the fields and hash them
      std::vector<Value> hashInputs{};
      for(auto const& fieldNameAttr : op.fields()) {

        auto fieldName = fieldNameAttr.dyn_cast_or_null<StringAttr>();
        auto field = fields.find(fieldName.getValue().str());

        auto extractedVal = rewriter.create<database::ExtractFieldFromTupleOp>(
            op.getLoc(), operands[0], field->first, field->second);
        hashInputs.emplace_back(extractedVal.getResult());
      }
      auto hashResult =
          rewriter.create<database::HashValuesOp>(op.getLoc(), rewriter.getIndexType(), hashInputs);

      // Store the current relation index in the map for this hash
      rewriter.create<database::InsertIntoHashTableOp>(
          op.getLoc(), hashResult.getResult(), insertedLocation,
          rewriter.getI64IntegerAttr(op.table()), rewriter.getI64IntegerAttr(tupleStreamIndex));

      rewriter.restoreInsertionPoint(savedInsertionPoint);
    }

    rewriter.create<database::FinalizeBuilderOp>(op.getLoc(), op.table(), "finalizeHashBuilder");
    rewriter.eraseOp(op);

    return success();
  }
};

struct GroupByOpLowering : public OpConversionPattern<database::GroupByOp> {
  using OpConversionPattern::OpConversionPattern;

  static boss::Expression defaultValueFromType(database::GroupByOp& op) {
    auto aggregateFuncName = op.aggregationFunctions()[0].dyn_cast<StringAttr>();
    auto aggregateFunc = ::mlir::dyn_cast<FuncOp>(
        op.getParentOfType<ModuleOp>().lookupSymbol(aggregateFuncName.getValue()));
    auto aggFuncType = aggregateFunc.getType();

    auto runtimeType =
        boss::mlir::conversion::mlirTypeToRuntimeType(aggFuncType.getResult(0), false);
    switch(runtimeType) {
    case boss::mlir::types::RuntimeTypes::INT:
      return 0;
    case boss::mlir::types::RuntimeTypes::STRING:
      return "";
    case boss::mlir::types::RuntimeTypes::INT64:
      return static_cast<size_t>(0);
    default:
      throw std::runtime_error("Unknown aggregate type");
    }
  }

  LogicalResult matchAndRewrite(database::GroupByOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto loc = op.getLoc();
    auto* context = op.getContext();

    auto initialValue = defaultValueFromType(op);
    // TODO free somewhere
    auto* hashTable = new runtime::aggregate::HashAggregate{initialValue};
    auto hashTablePtr = reinterpret_cast<size_t>(hashTable);
    auto hashTableAttr = ::mlir::IntegerAttr::get(IndexType::get(context), hashTablePtr);

    // Process all partitions
    for(auto i = 0UL; i < operands.size(); i++) {
      auto savedInsertionPoint = rewriter.saveInsertionPoint();
      rewriter.setInsertionPointAfter(operands[i].getDefiningOp());

      auto tupleStreamType = op.getOperand(i).getType().dyn_cast_or_null<TupleStreamType>();
      auto tupleStream = op.getOperand(i);
      auto tupleFields = tupleStreamType.getFields();

      // Hash the grouping fields
      std::vector<Value> valuesToHash;
      for(auto const& field : op.groupingFields()) {
        auto const& fieldName = field.dyn_cast<StringAttr>().getValue().str();
        auto type = tupleFields.find(fieldName);
        auto value = rewriter.create<database::ExtractFieldFromTupleOp>(loc, tupleStream, fieldName,
                                                                        type->second);
        valuesToHash.emplace_back(value.getResult());
      }
      auto hashed = rewriter.create<database::HashValuesOp>(loc, IndexType::get(op.getContext()),
                                                            valuesToHash);

      // Compute aggregate value
      // Get the aggregation function
      auto aggregateFuncName = op.aggregationFunctions()[i].dyn_cast<StringAttr>();
      auto aggregateFunc = ::mlir::dyn_cast<FuncOp>(
          op.getParentOfType<ModuleOp>().lookupSymbol(aggregateFuncName.getValue()));
      auto aggFuncType = aggregateFunc.getType();

      // Get the old value
      auto oldValue = rewriter.create<database::GroupByGet>(loc, aggFuncType.getResult(0),
                                                            hashed.getResult(), hashTableAttr);

      // Compute the new value
      std::vector<Value> aggregationValues;
      for(auto const& field : op.aggregationFields()) {
        auto const& fieldName = field.dyn_cast<StringAttr>().getValue().str();
        auto type = tupleFields.find(fieldName);
        if(type == tupleFields.end()) {
          // In this case, it is the lambda parameter
          aggregationValues.push_back(oldValue.getResult());
        } else {
          // In this case, it is a field of the tuple
          auto value = rewriter.create<database::ExtractFieldFromTupleOp>(loc, tupleStream,
                                                                          fieldName, type->second);
          aggregationValues.push_back(value.getResult());
        }
      }

      auto newValue = rewriter.create<CallOp>(loc, aggregateFunc, ValueRange(aggregationValues));

      rewriter.create<database::GroupByInsert>(loc, hashed.getResult(), newValue.getResult(0),
                                               hashTableAttr);

      rewriter.restoreInsertionPoint(savedInsertionPoint);
    }

    rewriter.replaceOpWithNewOp<ConstantIndexOp>(op, hashTablePtr);

    return success();
  }
};

struct SelectionOpLowering : public OpConversionPattern<database::SelectionOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::SelectionOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto savedPoint = rewriter.saveInsertionPoint();
    rewriter.setInsertionPointAfter(operands[0].getDefiningOp());
    auto fields = op.getAttr("fields").dyn_cast_or_null<ArrayAttr>();
    if(!fields) {
      op.emitError("Expecting attribute fields to exist in selection op");
      return failure();
    }

    auto tupleStream = operands[0].getType().dyn_cast_or_null<TupleStreamType>();
    if(!tupleStream) {
      return failure();
    }

    auto const& tupleStreamFields = tupleStream.getFields();

    // Load all the arguments to the selection function
    std::vector<mlir::Value> arguments;
    for(auto const& field : fields) {
      auto fieldName = field.dyn_cast_or_null<StringAttr>();
      if(!fieldName) {
        op.emitError("Expecting fields array to contain strings only");
        return failure();
      }
      auto databaseField = tupleStreamFields.find(fieldName.getValue().str());
      auto loadedValue = rewriter.create<database::ExtractFieldFromTupleOp>(
          op.getLoc(), operands[0], databaseField->first, databaseField->second);
      arguments.emplace_back(loadedValue.getResult());
    }

    // Call the selection function
    auto funcName = op.getAttr("selectionFunctionName").dyn_cast_or_null<StringAttr>();
    if(!funcName) {
      op.emitError("Expecting a string attribute selectionFunctionName");
      return failure();
    }
    auto* opaqueFuncOp = op.getParentOfType<ModuleOp>().lookupSymbol(funcName.getValue());
    auto funcOp = ::mlir::dyn_cast_or_null<FuncOp>(opaqueFuncOp);
    if(!funcOp) {
      op.emitError("Expecting a function named " + funcName.getValue().str());
      return failure();
    }
    auto callResult = rewriter.create<CallOp>(op.getLoc(), funcOp, arguments);

    // Add branch based on selection result
    auto ifOp = rewriter.create<scf::IfOp>(op.getLoc(), callResult.getResult(0), false);
    rewriter.setInsertionPointToStart(ifOp.getBody());

    // Carry over all the entire tuple stream
    std::vector<mlir::Value> newValues;
    for(auto const& [name, type] : tupleStreamFields) {
      auto extractedVal = rewriter.create<database::ExtractFieldFromTupleOp>(
          op.getLoc(), op.getOperand(), name, type);
      newValues.push_back(extractedVal.getResult());
    }

    auto tupleStreamVal =
        rewriter.create<database::PackFieldsIntoTupleOp>(op.getLoc(), newValues, tupleStream);
    rewriter.replaceOp(op, tupleStreamVal.getResult());

    rewriter.restoreInsertionPoint(savedPoint);

    return success();
  }
};

void DatabaseLoweringPass::runOnOperation() {
  ConversionTarget target(getContext());
  target.addLegalOp<ModuleOp, ModuleTerminatorOp>();

  target.addIllegalDialect<mlir::database::DatabaseDialect>();
  target
      .addLegalOp<database::ExtractFieldFromTupleOp, database::PackFieldsIntoTupleOp,
                  database::CreateUnionTupleStream, database::GetTupleStreamFromUnion,
                  database::FinalizeBuilderOp, database::AppendToRelationOp, database::HashValuesOp,
                  database::InsertIntoHashTableOp, database::AdvanceBuilderOp,
                  database::FindInHashTableOp, database::GroupByInsert, database::GroupByGet, database::LoadArrayIndirectOp>();

  target.addLegalDialect<::mlir::scf::SCFDialect>();
  target.addLegalDialect<::mlir::StandardOpsDialect>();
  target.addLegalDialect<memory::MemoryDialect>();

  OwningRewritePatternList patterns;

  TypeConverter typeConverter;

  typeConverter.addConversion([](Type t) { return t; });

  typeConverter.addConversion([&](RelationType t) {
    // TODO use correct type
    return mlir::IndexType::get(t.getContext());
  });

  target.addDynamicallyLegalOp<mlir::FuncOp>([&](Operation* op) {
    auto funcType = mlir::dyn_cast<mlir::FuncOp>(op).getType();
    for(auto const& result : funcType.getResults()) {
      if(result.isa<RelationType>()) {
        return false;
      }
    }
    return true;
  });

  patterns.insert<LookupJoinOpLowering, NextValueOpLowering>(&getContext(), database);
  patterns
      .insert<ProjectionOpLowering, SelectionOpLowering, BuildJoinOpLowering, GroupByOpLowering>(
          &getContext());
  patterns.insert<FuncOpSignatureConversion, CollectTuplesOpLowering>(&getContext(), typeConverter);
  patterns.insert<GetRelationOpLowering>(&getContext(), database, symbolTable);

  // Transitively lower symbol operations
  populateSymbolToStdPatterns(patterns, typeConverter, &getContext());

  auto module = getOperation();

  if(failed(applyPartialConversion(module, target, std::move(patterns)))) {
    signalPassFailure();
  }
}

// namespace
} // namespace

std::unique_ptr<mlir::Pass>
createLowerToDatabasePass(new_runtime::Database& database,
                          std::unordered_map<std::string, boss::Expression> symbolTable) {
  return std::make_unique<DatabaseLoweringPass>(database, symbolTable);
}
