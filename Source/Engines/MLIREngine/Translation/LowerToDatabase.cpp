#include "LowerDatabase.hpp"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include <iostream>
#include <mutex>
#include <mlir/Conversion/SCFToStandard/SCFToStandard.h>
#include <mlir/Conversion/StandardToLLVM/ConvertStandardToLLVM.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/Dialect/SCF/SCF.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>

namespace {
using namespace mlir;

std::mutex printMutex;

struct DatabaseLoweringPass
    : public PassWrapper<DatabaseLoweringPass, OperationPass<ModuleOp>> {

  DatabaseLoweringPass(runtime::Database& database) : database(database){};

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }
  void runOnOperation() final;

  runtime::Database& database;
};

struct CollectTuplesOpLowering : public OpConversionPattern<database::CollectTuplesOp> {
  CollectTuplesOpLowering(MLIRContext* ctx, TypeConverter& converter) : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(database::CollectTuplesOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    auto tupleStream = op.getOperand().getType().cast<TupleStreamType>();

    auto firstFieldName = tupleStream.getConcreteTupleTypes().front().first;
    auto firstFieldType = converter.convertType(tupleStream.getConcreteTupleTypes().front().second);

    // TODO correct implementation

    rewriter.replaceOpWithNewOp<database::ExtractFieldFromTupleOp>(op, op.getOperand(), firstFieldName, firstFieldType);

    return success();
  }

  TypeConverter& converter;
};

struct GetRelationOpLowering : public OpConversionPattern<database::GetRelationOp> {
  GetRelationOpLowering(MLIRContext* ctx, TypeConverter& converter, runtime::Database& database)
      : OpConversionPattern(ctx), converter(converter), database(database) {}

  LogicalResult matchAndRewrite(database::GetRelationOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    mlir::SmallVector<Value, 4> newValues;

    auto relation = database.getRelation(op.relationName().str());
    auto resultTupleStream = op.getTupleStream();

    for(auto const& [name, type] : resultTupleStream.getConcreteTupleTypes()) {
      // TODO change symbolic to false, maybe, yes. Maybe means some value of tuple is symbolic.
      auto arrayPtr = relation.getColumnDataPtr(name, false);

      // TODO: Generate different code depending on type (eg string, booleans)

      // TODO: Chunked array chunks?
      auto rawBuffer = boss::mlir::conversion::mlirTypeToArrowRawBuffer(arrayPtr.get(), type, 0);

      auto bufferAddress = rewriter.create<ConstantIntOp>(
          op.getLoc(), rawBuffer, sizeof(size_t) * 8);
      auto ptr = rewriter.create<LLVM::IntToPtrOp>(
          op.getLoc(),
          LLVM::LLVMPointerType::get(converter.convertType(type).cast<LLVM::LLVMType>()),
          bufferAddress.getResult());

      auto value = rewriter.create<LLVM::LoadOp>(op.getLoc(), ptr);
      newValues.push_back(value);
    }

    rewriter.replaceOpWithNewOp<database::PackFieldsIntoTupleOp>(op, newValues, resultTupleStream);

    return success();
  }

  TypeConverter& converter;
  runtime::Database& database;
};


void DatabaseLoweringPass::runOnOperation() {
  LLVMConversionTarget target(getContext());
  target.addLegalOp<ModuleOp, ModuleTerminatorOp>();

  LowerToLLVMOptions options{};
  options.useBarePtrCallConv = true;

  LLVMTypeConverter typeConverter(&getContext(), options);

  typeConverter.addConversion([context = &getContext()](RelationType /*t*/) -> llvm::Optional<Type> {
    // TODO change to correct type
    return mlir::LLVM::LLVMIntegerType::get(context, 32);
  });

  target.addLegalOp<database::ExtractFieldFromTupleOp>();
  target.addLegalOp<database::PackFieldsIntoTupleOp>();

//  target.addLegalDialect<::mlir::scf::SCFDialect>();
//  target.addLegalDialect<::mlir::StandardOpsDialect>();

  OwningRewritePatternList patterns;
  // TODO try to propagate type changes without complete lowering to LLVM
  populateLoopToStdConversionPatterns(patterns, &getContext());
  populateStdToLLVMConversionPatterns(typeConverter, patterns);

  patterns.insert<GetRelationOpLowering>(&getContext(), typeConverter, database);
  patterns.insert<CollectTuplesOpLowering>(&getContext(), typeConverter);

  auto module = getOperation();

  printMutex.lock();
  module.dump();
  printMutex.unlock();

  if(failed(applyFullConversion(module, target, std::move(patterns)))) {
    signalPassFailure();
  }
}

// namespace
} // namespace

std::unique_ptr<mlir::Pass> createLowerToDatabasePass(runtime::Database& database) {
  return std::make_unique<DatabaseLoweringPass>(database);
}
