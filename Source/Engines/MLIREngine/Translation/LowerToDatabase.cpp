#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseDialect.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryDialect.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
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
                           ConversionPatternRewriter& rewriter, mlir::Location loc)
        : baseArray(std::move(baseArray)), offset(offset), rewriter(rewriter), loc(loc) {}

    std::shared_ptr<arrow::Array> baseArray;
    mlir::Value offset;
    mlir::ConversionPatternRewriter& rewriter;
    mlir::Location loc;
    // TODO remaining types
    arrow::Status Visit(const arrow::Int32Type& /*type*/) override {
      auto integerArray = std::dynamic_pointer_cast<arrow::Int32Array>(baseArray);
      auto loadOp = rewriter.create<memory::LoadConstantAddressOp>(
          loc, reinterpret_cast<size_t>(integerArray->raw_values()), rewriter.getI32Type(), offset);
      result = loadOp.getResult();
      return arrow::Status::OK();
    }

    arrow::Status Visit(const arrow::FloatType& /*type*/) override {
      auto integerArray = std::dynamic_pointer_cast<arrow::FloatArray>(baseArray);
      auto loadOp = rewriter.create<memory::LoadConstantAddressOp>(
          loc, reinterpret_cast<size_t>(integerArray->raw_values()), rewriter.getF32Type(), offset);
      result = loadOp.getResult();
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
      ArrayLoaderTypeVisitor visitor(field, loop.getInductionVar(), rewriter, loc);
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
};

// struct ProjectionOpLowering : public OpConversionPattern<database::ProjectionOp> {
//  using OpConversionPattern::OpConversionPattern;
//
//  LogicalResult matchAndRewrite(database::ProjectionOp op, ArrayRef<Value> operands,
//                                ConversionPatternRewriter& rewriter) const override {
//    mlir::SmallVector<Value, 4> newValues;
//    auto outputTupleStream = op.getType().cast<TupleStreamUnionType>();
//
//    rewriter.setInsertionPointAfter(operands.front().getDefiningOp());
//
//    for(auto const& [name, type] : outputTupleStream.getConcreteTupleTypes()) {
//      auto extractedVal = rewriter.create<database::ExtractFieldFromTupleOp>(
//          op.getLoc(), op.getOperand(), name, type);
//      newValues.push_back(extractedVal.getResult());
//    }
//
//    rewriter.replaceOpWithNewOp<database::PackFieldsIntoTupleOp>(op, newValues,
//    outputTupleStream); return success();
//  }
//};

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
  patterns.insert<FuncOpSignatureConversion, CollectTuplesOpLowering>(&getContext(), typeConverter);
  //  patterns.insert<ProjectionOpLowering>(&getContext());

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
