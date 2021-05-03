#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseDialect.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryDialect.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"
#include "SexprToStd.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <llvm/ADT/StringExtras.h>
#include <mlir/Dialect/SCF/SCF.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/IR/StandardTypes.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mutex>
#include <queue>
#include <stack>
#include <string>

// Lowers all operations to the Std dialect. Converts types to Std dialect
// compatible types.

namespace {
using namespace mlir;

std::atomic<int> stringCounter{0};

std::mutex printMutex;

struct EndOpLowering : public OpConversionPattern<sexpr::EndOp> {
  EndOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(sexpr::EndOp o, ArrayRef<Value> /*operands*/,
                                ConversionPatternRewriter& rewriter) const override {
    rewriter.replaceOpWithNewOp<mlir::ReturnOp, ValueRange>(o.getOperation(), o.getOperand());

    return success();
  }

  TypeConverter& converter;
};

/// Create a loop to copy from memref source to memref target + offset
static LogicalResult createStringCopyLoop(Value const& source, Value const& target, int length,
                                          ConversionPatternRewriter& rewriter,
                                          Value const& targetOffset) {
  // Create loop
  auto lowerBound = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), 0);
  auto upperBound = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), length);
  auto step = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), 1);
  auto loop = rewriter.create<scf::ForOp>(rewriter.getUnknownLoc(), lowerBound, upperBound, step);
  PatternRewriter::InsertionGuard insertionGuard(rewriter);
  rewriter.setInsertionPointToStart(loop.getBody());

  // Load value from source + loop, store to target + loop + targetOffset
  auto load = rewriter.create<LoadOp, Value const&, ValueRange>(rewriter.getUnknownLoc(), source,
                                                                loop.getInductionVar());

  auto offset = rewriter.create<AddIOp, Type, Value, Value const&>(
      rewriter.getUnknownLoc(), rewriter.getIndexType(), loop.getInductionVar(), targetOffset);

  rewriter.create<StoreOp, Value, Value const&, ValueRange>(
      rewriter.getUnknownLoc(), load.getResult(), target, offset.getResult());

  return success();
}

struct SymbolOpLowering : public OpConversionPattern<sexpr::SymbolOp> {
  SymbolOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  template <typename BinaryOp>
  LogicalResult replaceBinaryOp(sexpr::SymbolOp& s, ConversionPatternRewriter& rewriter) const {
    rewriter.setInsertionPointAfter(s.getOperation());
    std::stack<Value> operandStack{};

    auto returnType = s.getResult().getType();
    auto newReturnType = converter.convertType(returnType);

    if(!newReturnType) {
      return failure();
    }

    // Create a stack
    // TODO: Re-order if the operation is commutative
    auto operands = s.getOperands();
    for(auto it = operands.end(); it != operands.begin();) {
      it--;
      operandStack.push(*it);
    }

    // Create pairwise binary operations - pop 2 results op1, op2, push
    // BinOp(op1, op2)
    while(operandStack.size() > 1) {
      auto op1 = operandStack.top();
      operandStack.pop();
      auto op2 = operandStack.top();
      operandStack.pop();

      auto newValue = rewriter.create<BinaryOp, TypeRange, Value&, Value&>(rewriter.getUnknownLoc(),
                                                                           newReturnType, op1, op2);

      operandStack.push(newValue);
    }

    rewriter.replaceOp(s.getOperation(), operandStack.top());

    return success();
  }

  LogicalResult rewriteStringJoin(sexpr::SymbolOp& s, ArrayRef<Value> operands,
                                  ConversionPatternRewriter& rewriter) const {
    // Find lenght of total joined string
    // Just nullbyte
    int64_t stringLength = 1;
    for(auto const& operand : operands) {
      if(operand.getType().dyn_cast<MemRefType>()) {
        // Don't add nullbyte of each string
        stringLength += operand.getType().dyn_cast<MemRefType>().getDimSize(0) - 1;
      } else {
        return failure();
      }
    }

    // Create new buffer
    auto memRefType = MemRefType::get({stringLength}, rewriter.getIntegerType(8));
    auto allocatedMemory =
        rewriter.create<AllocOp, MemRefType&>(rewriter.getUnknownLoc(), memRefType);

    int64_t offset = 0;
    for(auto const& operand : operands) {
      auto currentLength = operand.getType().cast<MemRefType>().getDimSize(0) - 1;

      auto offsetVal = rewriter.create<ConstantIndexOp, int64_t&>(rewriter.getUnknownLoc(), offset);
      createStringCopyLoop(operand, allocatedMemory.getResult(), currentLength, rewriter,
                           offsetVal);

      offset += currentLength;
    }
    auto zeroTerminator =
        rewriter.create<mlir::ConstantIntOp, int64_t, unsigned>(rewriter.getUnknownLoc(), 0, 8);
    auto endIndex =
        rewriter.create<mlir::ConstantIndexOp, int64_t>(rewriter.getUnknownLoc(), stringLength - 1);
    rewriter.create<StoreOp, Value, Value, ValueRange>(
        rewriter.getUnknownLoc(), zeroTerminator.getResult(), allocatedMemory.getResult(),
        endIndex.getResult());

    rewriter.replaceOp(s.getOperation(), allocatedMemory.getResult());
    return success();
  }

  template <CmpIPredicate cmpPred>
  LogicalResult replaceBooleanCompareOp(sexpr::SymbolOp s, ArrayRef<Value> operands,
                                        ConversionPatternRewriter& rewriter) const {
    auto cmpOp = rewriter.create<mlir::CmpIOp>(s.getLoc(), cmpPred, operands[0], operands[1]);
    rewriter.replaceOp(s.getOperation(), cmpOp.result());

    return success();
  }

  LogicalResult matchAndRewrite(sexpr::SymbolOp s, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    auto symbolName = s.name();

    // Dispatch table to insert appropriate
    const std::map<std::string, std::function<LogicalResult()>> dispatchTable{
        {"Plus", [&]() { return replaceBinaryOp<mlir::AddIOp>(s, rewriter); }},
        {"Minus", [&]() { return replaceBinaryOp<mlir::SubIOp>(s, rewriter); }},
        {"Mul", [&]() { return replaceBinaryOp<mlir::MulIOp>(s, rewriter); }},
        {"IDiv", [&]() { return replaceBinaryOp<mlir::SignedDivIOp>(s, rewriter); }},
        {"Eval",
         [&]() {
           rewriter.replaceOp(s.getOperation(), s.getOperands());
           return success();
         }},
        {"StringJoin", [&]() { return rewriteStringJoin(s, operands, rewriter); }},
        {"Greater",
         [&]() { return replaceBooleanCompareOp<CmpIPredicate::sgt>(s, operands, rewriter); }},
        {"Symbol",
         [&]() {
           auto allocOp =
               rewriter.create<memory::AllocateSymbolOp, Value const&>(s.getLoc(), operands[0]);
           rewriter.replaceOp(s.getOperation(), allocOp.getResult());

           return success();
         }},
        {"GetRelation",
         [&]() {
           auto stringVal = s.getOperand(0);
           auto stringConstantOp = stringVal.getDefiningOp<sexpr::StringConstantOp>();

           if(!stringConstantOp) {
             return failure();
           }

           auto relationName = stringConstantOp.value().str();
           auto tupleStreamUnion = converter.convertType(s.getResult().getType())
                                       .dyn_cast_or_null<TupleStreamUnionType>();

           if(!tupleStreamUnion) {
             return failure();
           }

           std::vector<mlir::Value> tupleStreamValues;
           for(auto i = 0U; i < tupleStreamUnion.getNumChildStreams(); i++) {
             auto const& tupleStream = tupleStreamUnion.getTupleStreams()[i];

             auto tupleStreamValue =
                 rewriter.create<database::GetRelationOp>(s.getLoc(), relationName, tupleStream, i);

             tupleStreamValues.emplace_back(tupleStreamValue.getResult());
           }

           auto tupleStreamUnionValue = rewriter.create<database::CreateUnionTupleStream>(
               s.getLoc(), tupleStreamUnion, tupleStreamValues);

           rewriter.replaceOp(s, tupleStreamUnionValue.getResult());

           return success();
         }},
        {"CollectTuples",
         [&]() {
           auto tupleStreamUnion = converter.convertType(operands[0].getType())
                                       .dyn_cast_or_null<TupleStreamUnionType>();

           if(!tupleStreamUnion) {
             return failure();
           }

           std::vector<mlir::Value> tupleStreamValues;
           for(auto i = 0UL; i < tupleStreamUnion.getNumChildStreams(); i++) {
             auto tupleStreamTy = tupleStreamUnion.getTupleStreams()[i];
             auto extractionOp = rewriter.create<database::GetTupleStreamFromUnion>(
                 s.getLoc(), tupleStreamTy, operands[0], i);
             tupleStreamValues.emplace_back(extractionOp.getResult());
           }

           rewriter.replaceOpWithNewOp<database::CollectTuplesOp>(
               s, RelationType::get(s.getContext()), tupleStreamValues);
           return success();
         }},
        {"Project",
         [&]() {
           rewriter.replaceOpWithNewOp<database::ProjectionOp>(
               s, converter.convertType(s.getType()), operands[1]);
           return success();
         }}

    };

    auto it = dispatchTable.find(std::string{symbolName});
    if(it != dispatchTable.end()) {
      // Function is defined, replace operation
      return it->second();
    }

    // Function is not defined here
    auto funcName = rewriter.create<sexpr::StringConstantOp>(s.getLoc(), std::string{symbolName});
    auto allocOp = rewriter.create<memory::AllocateSymbolicFunctionOp, Value, ValueRange>(
        s.getLoc(), funcName.getResult(), s.getOperands());

    rewriter.replaceOp(s.getOperation(), allocOp.getResult());

    return success();
  }

  TypeConverter& converter;
};

struct ConstantOpLowering : public OpConversionPattern<sexpr::IntegerConstantOp> {
  ConstantOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(sexpr::IntegerConstantOp s, ArrayRef<Value> /*operands*/,
                                ConversionPatternRewriter& rewriter) const override {

    auto newType = converter.convertType(s.getType());

    if(!newType) {
      return failure();
    }

    rewriter.replaceOpWithNewOp<mlir::ConstantIntOp, int64_t, Type&>(s.getOperation(), s.value(),
                                                                     newType);

    return success();
  }

  /// The type converter to use when rewriting the signature.
  TypeConverter& converter;
};

// Lowers a string constant by a) creating a global constant for it and b) copying it into memory
struct ConstantStringOpLowering : public OpConversionPattern<sexpr::StringConstantOp> {
  ConstantStringOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(sexpr::StringConstantOp op, ArrayRef<Value> /*operands*/,
                                ConversionPatternRewriter& rewriter) const override {

    // Length of the string with null terminator
    auto stringLength = op.value().size() + 1;
    auto memRefType =
        MemRefType::get({static_cast<int64_t>(stringLength)}, rewriter.getIntegerType(8));

    // Set insertino point to main module body to insert global string
    auto currentLocation = rewriter.saveInsertionPoint();
    rewriter.setInsertionPointToStart(op.getParentOfType<ModuleOp>().getBody());

    llvm::SmallVector<int8_t, 30> stringWithNullTerminator{op.value().bytes()};
    stringWithNullTerminator.push_back(0);

    int currentString = stringCounter++;

    // Create a global memory reference to our string
    rewriter.create<mlir::GlobalMemrefOp, ::llvm::StringRef, mlir::StringAttr, ::mlir::TypeAttr,
                    mlir::Attribute, bool>(
        rewriter.getUnknownLoc(), std::to_string(currentString), nullptr, TypeAttr::get(memRefType),
        DenseElementsAttr::get(
            RankedTensorType::get({static_cast<int64_t>(stringLength)}, rewriter.getIntegerType(8)),
            ArrayRef<int8_t>(stringWithNullTerminator)),
        false);

    // Restore insertion point to current function
    rewriter.restoreInsertionPoint(currentLocation);

    // Get memory for a) global string just created and b) local copy
    auto memref = rewriter.create<mlir::GetGlobalMemrefOp, Type&, StringRef>(
        rewriter.getUnknownLoc(), memRefType, std::to_string(currentString));
    auto allocatedMemory =
        rewriter.create<AllocOp, MemRefType&>(rewriter.getUnknownLoc(), memRefType);
    auto offset = rewriter.create<ConstantIndexOp, int64_t>(op.getLoc(), 0);
    createStringCopyLoop(memref.getResult(), allocatedMemory.getResult(), stringLength, rewriter,
                         offset.getResult());

    rewriter.replaceOp(op.getOperation(), allocatedMemory.getResult());

    return success();
  }

  TypeConverter& converter;
};

struct SexprToStdLoweringPass : public PassWrapper<SexprToStdLoweringPass, FunctionPass> {

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<mlir::StandardOpsDialect>();
  }

  void runOnFunction() final;
};

struct CallOpSignatureConversion : public OpConversionPattern<mlir::CallOp> {
  CallOpSignatureConversion(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  /// Hook for derived classes to implement combined matching and rewriting.
  LogicalResult matchAndRewrite(mlir::CallOp callOp, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    FunctionType type = callOp.getCalleeType();

    // Convert the original function results.
    SmallVector<Type, 1> convertedResults;
    if(failed(converter.convertTypes(type.getResults(), convertedResults))) {
      return failure();
    }

    // Substitute with the new result types from the corresponding FuncType
    // conversion.
    rewriter.replaceOpWithNewOp<mlir::CallOp>(callOp, callOp.callee(), convertedResults, operands);
    return success();
  }

  /// The type converter to use when rewriting the signature.
  TypeConverter& converter;
};

} // namespace

void SexprToStdLoweringPass::runOnFunction() {
  ConversionTarget target(getContext());

  // Create type converter
  TypeConverter c;
  OwningRewritePatternList patterns;

  c.addConversion([](Type type) {
    return type;
  });

  populateSymbolToStdPatterns(patterns, c, &getContext());

  // Register legality of dialects and operations
  target.addLegalDialect<mlir::StandardOpsDialect, mlir::scf::SCFDialect,
                         mlir::memory::MemoryDialect, database::DatabaseDialect>();
  target.addIllegalDialect<sexpr::SExprDialect>();
  target.addLegalOp<FuncOp>();
  target.addDynamicallyLegalOp<mlir::CallOp>(
      // CallOp is legal in source and target, but the type should be converted
      [&](mlir::CallOp op) { return c.isLegal(op); });

  patterns.insert<CallOpSignatureConversion>(&getContext(), c);

  auto res = applyPartialConversion(getFunction(), target, std::move(patterns));

  // Convert the type of the current function
  auto newFuncType = c.convertType(getFunction().getType()).dyn_cast<FunctionType>();

  if(newFuncType) {
    getFunction().setType(newFuncType);
  }

  if(failed(res)) {
    signalPassFailure();
  }
}

std::unique_ptr<mlir::Pass> createLowerToStdPass() {
  return std::make_unique<SexprToStdLoweringPass>();
}

void populateSymbolToStdPatterns(OwningRewritePatternList& patterns, TypeConverter& c, MLIRContext* context) {
  c.addConversion([&c](SymbolOrValueType t) -> llvm::Optional<Type> {
    if(t.isSymbolic() == sexprtype::SymbolOrValue::VALUE) {
      return c.convertType(t.getBaseType());
    }
    return llvm::Optional<Type>{};
  });
  c.addConversion([&c](FunctionType t) -> llvm::Optional<Type> {
    auto resultTypes = t.getResults();
    llvm::SmallVector<Type, 1> convertedTypes{};

    c.convertTypes(resultTypes, convertedTypes);

    return FunctionType::get(t.getInputs(), convertedTypes, t.getContext());
  });
  c.addConversion([](StringType t) -> llvm::Optional<Type> {
    // Return memory buffer of length + 1 to account for null byte
    return MemRefType::get({static_cast<int64_t>(t.getLength() + 1)},
                           IntegerType::get(8, t.getContext()));
  });

  patterns.insert<SymbolOpLowering, EndOpLowering, ConstantOpLowering, ConstantStringOpLowering>(
      context, c);
}
