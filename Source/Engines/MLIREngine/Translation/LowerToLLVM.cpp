#include "Engines/MLIREngine/Dialect/MemoryOps.h"
#include <mlir/Conversion/SCFToStandard/SCFToStandard.h>
#include <mlir/Conversion/StandardToLLVM/ConvertStandardToLLVM.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>

namespace {
using namespace mlir;

/// Return a symbol reference to the printf function, inserting it into the
/// module if necessary.
static FlatSymbolRefAttr getOrInsertPrintf(PatternRewriter& rewriter, ModuleOp module) {
  auto* context = module.getContext();
  if(module.lookupSymbol<LLVM::LLVMFuncOp>("printf"))
    return SymbolRefAttr::get("printf", context);

  // Create a function declaration for printf, the signature is:
  //   * `i32 (i8*, ...)`
  auto llvmI32Ty = LLVM::LLVMType::getInt32Ty(context);
  auto llvmI8PtrTy = LLVM::LLVMType::getInt8PtrTy(context);
  auto llvmFnType = LLVM::LLVMType::getFunctionTy(llvmI32Ty, llvmI8PtrTy,
                                                  /*isVarArg=*/true);

  // Insert the printf function into the body of the parent module.
  PatternRewriter::InsertionGuard insertGuard(rewriter);
  rewriter.setInsertionPointToStart(module.getBody());
  rewriter.create<LLVM::LLVMFuncOp>(module.getLoc(), "printf", llvmFnType);
  return SymbolRefAttr::get("printf", context);
}

/// Return a value representing an access into a global string with the given
/// name, creating the string if necessary.
static Value getOrCreateGlobalString(Location loc, OpBuilder& builder, StringRef name,
                                     StringRef value, ModuleOp module) {
  // Create the global at the entry of the module.
  LLVM::GlobalOp global;
  if(!(global = module.lookupSymbol<LLVM::GlobalOp>(name))) {
    OpBuilder::InsertionGuard insertGuard(builder);
    builder.setInsertionPointToStart(module.getBody());
    auto type =
        LLVM::LLVMType::getArrayTy(LLVM::LLVMType::getInt8Ty(builder.getContext()), value.size());
    global = builder.create<LLVM::GlobalOp>(loc, type, /*isConstant=*/true, LLVM::Linkage::Internal,
                                            name, builder.getStringAttr(value));
  }

  // Get the pointer to the first character in the global string.
  Value globalPtr = builder.create<LLVM::AddressOfOp>(loc, global);
  Value cst0 =
      builder.create<LLVM::ConstantOp>(loc, LLVM::LLVMType::getInt64Ty(builder.getContext()),
                                       builder.getIntegerAttr(builder.getIndexType(), 0));
  return builder.create<LLVM::GEPOp>(loc, LLVM::LLVMType::getInt8PtrTy(builder.getContext()),
                                     globalPtr, ArrayRef<Value>({cst0, cst0}));
}

struct SexprToLLVMLoweringPass
    : public PassWrapper<SexprToLLVMLoweringPass, OperationPass<ModuleOp>> {
  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }
  void runOnOperation() final;
};

struct PrintMemrefOpLowering : public OpConversionPattern<memory::PrintMemrefOp> {
  PrintMemrefOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(memory::PrintMemrefOp printOp, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    auto loc = printOp.getLoc();
    ModuleOp parentModule = printOp.getParentOfType<ModuleOp>();

    Value formatSpecifierCst =
        getOrCreateGlobalString(loc, rewriter, "frmt_spec", StringRef("%s \0", 4), parentModule);
    auto printfRef = getOrInsertPrintf(rewriter, parentModule);

    rewriter.create<LLVM::CallOp>(loc, LLVM::LLVMType::getInt32Ty(rewriter.getContext()), printfRef,
                                  ArrayRef<Value>({formatSpecifierCst, operands[0]}));

    rewriter.eraseOp(printOp.getOperation());

    return success();
  }

  TypeConverter& converter;
};

void SexprToLLVMLoweringPass::runOnOperation() {
  LLVMConversionTarget target(getContext());
  target.addLegalOp<ModuleOp, ModuleTerminatorOp>();

  LowerToLLVMOptions options{};
  options.useBarePtrCallConv = true;

  LLVMTypeConverter typeConverter(&getContext(), options);

  OwningRewritePatternList patterns;
  populateLoopToStdConversionPatterns(patterns, &getContext());
  populateStdToLLVMConversionPatterns(typeConverter, patterns);

  patterns.insert<PrintMemrefOpLowering>(&getContext(), typeConverter);

  auto module = getOperation();
  if(failed(applyFullConversion(module, target, std::move(patterns)))) {
    signalPassFailure();
  }
}
}; // namespace

std::unique_ptr<mlir::Pass> createLowerToLLVMPass() {
  return std::make_unique<SexprToLLVMLoweringPass>();
}
