#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseDialect.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryDialect.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
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

  DatabaseLoweringPass(runtime::Database& database) : database(database){};

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }
  void runOnOperation() final;

  runtime::Database& database;
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

    auto savedInsertionPoint = rewriter.saveInsertionPoint();
    rewriter.setInsertionPointAfter(operands.front().getDefiningOp());

    auto tupleStream = op.getOperand().getType().cast<TupleStreamType>();

    auto* schema = boss::mlir::conversion::tupleStreamTypeToArrowSchema(tupleStream);
    auto* builders = runtime::getBuildersForSchema(schema);

    for(auto const& [name, type] : tupleStream.getConcreteTupleTypes()) {
      auto extractedVal = rewriter.create<database::ExtractFieldFromTupleOp>(
          op.getLoc(), op.getOperand(), name, type);

      auto builderPtr = reinterpret_cast<size_t>(builders->operator[](name));
      rewriter.create<database::AppendToRelationOp>(
          op.getLoc(), extractedVal.getResult(), builderPtr,
          boss::mlir::conversion::mlirTypeToRuntimeType(type, false));
    }

    rewriter.restoreInsertionPoint(savedInsertionPoint);

    auto newOp = rewriter.create<database::FinalizeRelationOp>(
        op.getLoc(), reinterpret_cast<size_t>(builders), reinterpret_cast<size_t>(schema));

    op.dump();
    newOp.dump();

    rewriter.replaceOp(op, newOp.getResult());
    return success();
  }

  TypeConverter& converter;
};

struct GetRelationOpLowering : public OpConversionPattern<database::GetRelationOp> {
  GetRelationOpLowering(MLIRContext* ctx, runtime::Database& database)
      : OpConversionPattern(ctx), database(database) {}

  LogicalResult matchAndRewrite(database::GetRelationOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    mlir::SmallVector<Value, 4> newValues;

    auto resultTupleStream =
        rewriter.getType<TupleStreamType>(op.getTupleStream().getTupleTypes());

    auto relation = database.getRelation(op.relationName().str());
    // TODO loop for different types
    auto relationLength = relation.getLength();

    // Create loop
    auto lowerBound = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), 0);
    auto upperBound = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), relationLength);
    auto step = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), 1);
    auto loop = rewriter.create<scf::ForOp>(rewriter.getUnknownLoc(), lowerBound, upperBound, step);
    PatternRewriter::InsertionGuard insertionGuard(rewriter);
    rewriter.setInsertionPointToStart(loop.getBody());

    for(auto const& [name, type] : resultTupleStream.getConcreteTupleTypes()) {
      // TODO change symbolic to false, maybe, yes. Maybe means some value of tuple is symbolic.
      auto arrayPtr = relation.getColumnDataPtr(name, false);

      // TODO: Generate different code depending on type (eg string, booleans)

      // TODO: Chunked array chunks?
      auto rawBuffer = boss::mlir::conversion::mlirTypeToArrowRawBuffer(arrayPtr.get(), type, 0);

      auto value = rewriter.create<memory::LoadConstantAddressOp>(op.getLoc(), rawBuffer, type, loop.getInductionVar());
      newValues.push_back(value);
    }

    rewriter.replaceOpWithNewOp<database::PackFieldsIntoTupleOp>(op, newValues, resultTupleStream);

    return success();
  }

  runtime::Database& database;
};

void DatabaseLoweringPass::runOnOperation() {
  ConversionTarget target(getContext());
  target.addLegalOp<ModuleOp, ModuleTerminatorOp>();

  target.addIllegalDialect<mlir::database::DatabaseDialect>();
  target.addLegalOp<database::ExtractFieldFromTupleOp, database::PackFieldsIntoTupleOp,
                    database::FinalizeRelationOp, database::AppendToRelationOp>();

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

  auto module = getOperation();

  if(failed(applyPartialConversion(module, target, std::move(patterns)))) {
    signalPassFailure();
  }

  module.dump();
}

// namespace
} // namespace

std::unique_ptr<mlir::Pass> createLowerToDatabasePass(runtime::Database& database) {
  return std::make_unique<DatabaseLoweringPass>(database);
}
