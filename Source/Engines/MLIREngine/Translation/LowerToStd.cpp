#include "Engines/MLIREngine/Dialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprTypes.h"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"
#include <array>
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

std::mutex printMutex;

// Lowers all operations to the Std dialect. Converts types to Std dialect
// compatible types.

namespace {
using namespace mlir;

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

  LogicalResult matchAndRewrite(sexpr::SymbolOp s, ArrayRef<Value> /*operands*/,
                                ConversionPatternRewriter& rewriter) const override {

    auto symbolName = s.name();

    if(symbolName == "Plus") {
      return replaceBinaryOp<mlir::AddIOp>(s, rewriter);
    }
    if(symbolName == "Minus") {
      return replaceBinaryOp<mlir::SubIOp>(s, rewriter);
    }
    if(symbolName == "Mul") {
      return replaceBinaryOp<mlir::MulIOp>(s, rewriter);
    }
    if(symbolName == "IDiv") {
      return replaceBinaryOp<mlir::SignedDivIOp>(s, rewriter);
    }

    return failure();
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

    auto currentLocation = rewriter.saveInsertionPoint();
    rewriter.setInsertionPointToStart(op.getParentOfType<ModuleOp>().getBody());

    llvm::SmallVector<int8_t, 30> stringWithNullTerminator{op.value().bytes()};
    stringWithNullTerminator.push_back(0);

    // Create a global memory reference to our string
    rewriter.create<mlir::GlobalMemrefOp, ::llvm::StringRef, mlir::StringAttr, ::mlir::TypeAttr,
                    mlir::Attribute, bool>(
        rewriter.getUnknownLoc(), "string1", nullptr, TypeAttr::get(memRefType),
        DenseElementsAttr::get(
            RankedTensorType::get({static_cast<int64_t>(stringLength)}, rewriter.getIntegerType(8)),
            ArrayRef<int8_t>(stringWithNullTerminator)),
        false);

    rewriter.restoreInsertionPoint(currentLocation);

    auto memref = rewriter.create<mlir::GetGlobalMemrefOp, Type&, StringRef>(
        rewriter.getUnknownLoc(), memRefType, "string1");

    auto allocatedMemory =
        rewriter.create<AllocOp, MemRefType&>(rewriter.getUnknownLoc(), memRefType);

    rewriter.setInsertionPointAfter(allocatedMemory);

    auto lowerBound = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), 0);
    auto upperBound = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), stringLength);
    auto step = rewriter.create<ConstantIndexOp>(rewriter.getUnknownLoc(), 1);
    auto loop = rewriter.create<scf::ForOp>(rewriter.getUnknownLoc(), lowerBound, upperBound, step);
    rewriter.replaceOp(op.getOperation(), allocatedMemory.getResult());

    auto insertionPoint = rewriter.saveInsertionPoint();
    rewriter.setInsertionPointToStart(loop.getBody());
    auto load = rewriter.create<LoadOp, Value, ValueRange>(
        rewriter.getUnknownLoc(), memref.getResult(), loop.getInductionVar());
    rewriter.create<StoreOp, Value, Value, ValueRange>(rewriter.getUnknownLoc(), load.getResult(),
                                                       allocatedMemory.getResult(),
                                                       loop.getInductionVar());

    rewriter.restoreInsertionPoint(insertionPoint);

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

  c.addConversion([](Type t) -> llvm::Optional<Type> { return t; });
  c.addConversion([&c](SymbolOrValueType t) -> llvm::Optional<Type> {
    if(t.isSymbolic() == sexprtype::SymbolOrValue::VALUE) {
      return c.convertType(t.getBaseType());
    }
    return llvm::None;
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

  // Convert the type of the current function
  auto newFuncType = c.convertType(getFunction().getType()).dyn_cast<FunctionType>();

  if(newFuncType) {
    getFunction().setType(newFuncType);
  }

  // Register legality of dialects and operations
  target.addLegalDialect<mlir::StandardOpsDialect, mlir::scf::SCFDialect>();
  target.addIllegalDialect<sexpr::SExprDialect>();
  target.addLegalOp<FuncOp>();
  target.addDynamicallyLegalOp<mlir::CallOp>(
      // CallOp is legal in source and target, but the type should be converted
      [&](mlir::CallOp op) { return c.isLegal(op); });

  OwningRewritePatternList patterns;
  patterns.insert<SymbolOpLowering, EndOpLowering, ConstantOpLowering, ConstantStringOpLowering>(
      &getContext(), c);

  patterns.insert<CallOpSignatureConversion>(&getContext(), c);

  auto res = applyPartialConversion(getFunction(), target, std::move(patterns));

  if(failed(res)) {
    signalPassFailure();
  }

  // printMutex.lock();
  // getFunction().dump();
  // printMutex.unlock();
}

std::unique_ptr<mlir::Pass> createLowerToStdPass() {
  return std::make_unique<SexprToStdLoweringPass>();
}
