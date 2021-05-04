#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseDialect.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryDialect.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include "LowerDatabase.hpp"
#include <iostream>
#include <mlir/Conversion/SCFToStandard/SCFToStandard.h>
#include <mlir/Conversion/StandardToLLVM/ConvertStandardToLLVM.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/SCF/SCF.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mutex>

namespace {
using namespace mlir;

std::mutex printMutex;

struct DatabaseLoweringPass : public PassWrapper<DatabaseLoweringPass, OperationPass<ModuleOp>> {

  DatabaseLoweringPass(new_runtime::Database& database) : database(database){};

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }
  void runOnOperation() final;

  new_runtime::Database& database;
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

struct CollectTuplesOpLowering : public OpConversionPattern<database::CollectTuplesOp> {
  CollectTuplesOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(database::CollectTuplesOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto loc = op.getLoc();

    // TODO free somewhere
    auto* relationBuilder = new new_runtime::RelationBuilder;

    // Iterate over all streams
    for(auto const& streamValue : operands) {
      auto tupleStreamType = streamValue.getType().dyn_cast_or_null<TupleStreamType>();
      if(!tupleStreamType) {
        op.emitError("Expected a tuple stream type.");
        return failure();
      }

      auto savedInsertionPoint = rewriter.saveInsertionPoint();
      rewriter.setInsertionPointAfter(streamValue.getDefiningOp());

      auto runtimeFields =
          boss::mlir::conversion::mlirFieldsToRuntimeFields(tupleStreamType.getFields());

      rewriter.create<database::AdvanceBuilderOp>(
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

        rewriter.create<database::AppendToRelationOp>(op.getLoc(), extractOp.getResult(),
                                                      reinterpret_cast<size_t>(columnBuilder.get()),
                                                      runtimeType);
      }

      rewriter.restoreInsertionPoint(savedInsertionPoint);
    }

    auto newOp = rewriter.create<database::FinalizeRelationOp>(
        op.getLoc(), reinterpret_cast<size_t>(relationBuilder));



    rewriter.replaceOp(op, newOp.getResult());
    newOp.getParentOp()->dump();
    return success();
  }

  TypeConverter& converter;
};

struct GetRelationOpLowering : public OpConversionPattern<database::GetRelationOp> {
  GetRelationOpLowering(MLIRContext* ctx, new_runtime::Database& database)
      : OpConversionPattern(ctx), database(database) {}

  // Generates code to load the type
  struct ArrayLoaderTypeVisitor : arrow::TypeVisitor {
    ::mlir::Value result;

    ArrayLoaderTypeVisitor(std::shared_ptr<arrow::Array> baseArray, mlir::Value offset,
                           ConversionPatternRewriter& rewriter, mlir::Location loc,
                           new_runtime::Database& database)
        : baseArray(std::move(baseArray)), offset(offset), rewriter(rewriter), loc(loc),
          database(database) {}

    std::shared_ptr<arrow::Array> baseArray;
    mlir::Value offset;
    mlir::ConversionPatternRewriter& rewriter;
    mlir::Location loc;
    new_runtime::Database& database;

    // TODO remaining types
    arrow::Status Visit(const arrow::Int32Type& /*type*/) override {
      auto integerArray = std::dynamic_pointer_cast<arrow::Int32Array>(baseArray);
      auto loadOp = rewriter.create<memory::LoadConstantAddressOp>(
          loc, reinterpret_cast<size_t>(integerArray->raw_values()), rewriter.getI32Type(), offset);
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
      auto structArray = std::dynamic_pointer_cast<arrow::StructArray>(baseArray);

      auto nameArray = std::dynamic_pointer_cast<arrow::DictionaryArray>(structArray->field(0));
      auto symbolName =
          std::dynamic_pointer_cast<arrow::StringArray>(nameArray->dictionary())->GetString(0);

      std::vector<Value> childResults;
      for(auto i = 1; i < type.num_fields(); i++) {
        auto const& field = type.field(i);

        ArrayLoaderTypeVisitor childVisitor(structArray->field(i), offset, rewriter, loc, database);
        auto status = field->type()->Accept(&childVisitor);
        if(!status.ok()) {
          return status;
        }

        childResults.emplace_back(childVisitor.result);
      }

      auto symbolOp = rewriter.create<sexpr::SymbolOp>(loc, symbolName, childResults);

      auto typedSymbolOp = mlir::dyn_cast_or_null<TypeInference>(symbolOp.getOperation());
      if(!typedSymbolOp) {
        return arrow::Status::TypeError("Could not cast symbol op to type inference interface");
      }
      typedSymbolOp.inferType(database);

      result = symbolOp.getResult();
      return arrow::Status::OK();
    }
  };

  LogicalResult matchAndRewrite(database::GetRelationOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    auto loc = op.getLoc();

    auto resultTupleStream = rewriter.getType<TupleStreamType>(op.getTupleStream().getFields());

    auto unionChildArray =
        database.getRelation(op.relationName().str()).get()->field(op.unionChildIndex());
    auto relationLength = unionChildArray->length() / unionChildArray->num_fields();
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
      ArrayLoaderTypeVisitor visitor(field, loop.getInductionVar(), rewriter, loc, database);
      auto status = field->type()->Accept(&visitor);
      if(!status.ok()) {
        throw std::runtime_error(status.message());
      }
      loadedValues.push_back(visitor.result);
    }

    rewriter.replaceOpWithNewOp<database::PackFieldsIntoTupleOp>(op, loadedValues,
                                                                 resultTupleStream);

    op.getParentOp()->dump();
    return success();
  }

  new_runtime::Database& database;
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

void DatabaseLoweringPass::runOnOperation() {
  ConversionTarget target(getContext());
  target.addLegalOp<ModuleOp, ModuleTerminatorOp>();

  target.addIllegalDialect<mlir::database::DatabaseDialect>();
  target.addLegalOp<database::ExtractFieldFromTupleOp, database::PackFieldsIntoTupleOp,
                    database::CreateUnionTupleStream, database::GetTupleStreamFromUnion,
                    database::FinalizeRelationOp, database::AppendToRelationOp,
                    database::AdvanceBuilderOp>();

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

  patterns.insert<GetRelationOpLowering>(&getContext(), database);
  patterns.insert<ProjectionOpLowering>(&getContext());
  patterns.insert<FuncOpSignatureConversion, CollectTuplesOpLowering>(&getContext(), typeConverter);

  // Transitively lower symbol operations
  populateSymbolToStdPatterns(patterns, typeConverter, &getContext());

  auto module = getOperation();

  if(failed(applyPartialConversion(module, target, std::move(patterns)))) {
    signalPassFailure();
  }
}

// namespace
} // namespace

std::unique_ptr<mlir::Pass> createLowerToDatabasePass(new_runtime::Database& database) {
  return std::make_unique<DatabaseLoweringPass>(database);
}
