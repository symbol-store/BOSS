#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include <iostream>
#include <mlir/Conversion/SCFToStandard/SCFToStandard.h>
#include <mlir/Conversion/StandardToLLVM/ConvertStandardToLLVM.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mutex>

namespace {
using namespace mlir;

std::mutex printMutex;

static const StringRef symbolStructName{"SymbolStruct"};
static const StringRef symbolArgStructName{"SymbolArgStruct"};
static const StringRef voidStructName{"VoidStruct"};

/// Insert or get a function into the llvm namespace
static FlatSymbolRefAttr getOrInsertFunction(std::string name, LLVM::LLVMType functionType,
                                             PatternRewriter& rewriter, ModuleOp module) {
  auto* context = module.getContext();
  if(module.lookupSymbol<LLVM::LLVMFuncOp>(name))
    return SymbolRefAttr::get(name, context);

  // Insert the printf function into the body of the parent module.
  PatternRewriter::InsertionGuard insertGuard(rewriter);
  rewriter.setInsertionPointToStart(module.getBody());
  rewriter.create<LLVM::LLVMFuncOp>(module.getLoc(), name, functionType);
  return SymbolRefAttr::get(name, context);
}

/// Return a symbol reference to the printf function, inserting it into the
/// module if necessary.
static FlatSymbolRefAttr getOrInsertPrintf(PatternRewriter& rewriter, ModuleOp module) {
  auto* context = module.getContext();
  // Create a function declaration for printf, the signature is:
  //   * `i32 (i8*, ...)`
  auto llvmI32Ty = LLVM::LLVMType::getInt32Ty(context);
  auto llvmI8PtrTy = LLVM::LLVMType::getInt8PtrTy(context);
  auto llvmFnType = LLVM::LLVMType::getFunctionTy(llvmI32Ty, llvmI8PtrTy,
                                                  /*isVarArg=*/true);

  return getOrInsertFunction("printf", llvmFnType, rewriter, module);
}

static FlatSymbolRefAttr getOrInsertAllocSymbol(PatternRewriter& rewriter, ModuleOp module) {
  auto* context = module.getContext();
  // Create signature
  auto argType = LLVM::LLVMType::getInt8PtrTy(context);
  auto returnType =
      LLVM::LLVMPointerType::get(LLVM::LLVMStructType::getIdentified(context, symbolStructName));
  auto funcType = LLVM::LLVMType::getFunctionTy(returnType, argType,
                                                /*isVarArg=*/false);

  return getOrInsertFunction("allocateSymbol", funcType, rewriter, module);
}

static FlatSymbolRefAttr getOrInsertAllocArgsSymbol(PatternRewriter& rewriter, ModuleOp module) {
  auto* context = module.getContext();
  // Create signature
  auto baseExprType =
      LLVM::LLVMPointerType::get(LLVM::LLVMStructType::getIdentified(context, symbolStructName));
  auto argcType = LLVM::LLVMIntegerType::get(context, 64);
  auto returnType = LLVM::LLVMVoidType::get(context);
  auto funcType = LLVM::LLVMType::getFunctionTy(returnType, {baseExprType, argcType},
                                                /*isVarArg=*/true);

  return getOrInsertFunction("setSExpressionArgs", funcType, rewriter, module);
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

  SexprToLLVMLoweringPass(runtime::Database& database) : database(database){};

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }
  void runOnOperation() final;

  runtime::Database& database;
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

struct LoadConstantAddressOpLowering : public OpConversionPattern<memory::LoadConstantAddressOp> {
  LoadConstantAddressOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(memory::LoadConstantAddressOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    auto bufferAddress = rewriter.create<LLVM::ConstantOp>(
        op.getLoc(), LLVM::LLVMType::getInt64Ty(rewriter.getContext()),
        rewriter.getIntegerAttr(rewriter.getIndexType(), op.address()));

    auto ptrType = LLVM::LLVMPointerType::get(converter.convertType(op.getType()).cast<LLVM::LLVMType>());

    auto basePtr = rewriter.create<LLVM::IntToPtrOp>(
        op.getLoc(),
        ptrType,
        bufferAddress.getResult());

    auto effectivePtr = rewriter.create<LLVM::GEPOp>(op.getLoc(), ptrType, basePtr.getResult(), operands.front());

    rewriter.replaceOpWithNewOp<LLVM::LoadOp>(op, effectivePtr);

    return success();
  }

  TypeConverter& converter;
};

struct AllocateSymbolOpLowering : public OpConversionPattern<memory::AllocateSymbolOp> {
  AllocateSymbolOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(memory::AllocateSymbolOp allocOp, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    auto* context = allocOp.getContext();
    auto loc = allocOp.getLoc();
    ModuleOp parentModule = allocOp.getParentOfType<ModuleOp>();

    auto allocExprRef = getOrInsertAllocSymbol(rewriter, parentModule);

    auto barePtr = rewriter.create<LLVM::ExtractValueOp, Type, Value const&, ArrayAttr>(
        loc, LLVM::LLVMType::getInt8PtrTy(context), operands[0], rewriter.getI64ArrayAttr(0));

    auto allocExprCall = rewriter.create<LLVM::CallOp>(
        loc,
        LLVM::LLVMPointerType::get(LLVM::LLVMStructType::getIdentified(context, symbolStructName)),
        allocExprRef, barePtr.getResult());

    rewriter.replaceOp(allocOp.getOperation(), allocExprCall.getResults());

    return success();
  }

  TypeConverter& converter;
};

struct AllocateSymbolicFunctionOpLowering
    : public OpConversionPattern<memory::AllocateSymbolicFunctionOp> {
  AllocateSymbolicFunctionOpLowering(MLIRContext* ctx, TypeConverter& converter)
      : OpConversionPattern(ctx), converter(converter) {}

  LogicalResult matchAndRewrite(memory::AllocateSymbolicFunctionOp allocOp,
                                ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {

    auto* context = allocOp.getContext();
    auto loc = allocOp.getLoc();
    ModuleOp parentModule = allocOp.getParentOfType<ModuleOp>();

    // Create a symbol for the name
    auto allocExprRef = getOrInsertAllocSymbol(rewriter, parentModule);

    auto barePtr = rewriter.create<LLVM::ExtractValueOp, Type, Value const&, ArrayAttr>(
        loc, LLVM::LLVMType::getInt8PtrTy(context), operands[0], rewriter.getI64ArrayAttr(0));

    auto allocExprCall = rewriter.create<LLVM::CallOp>(
        loc,
        LLVM::LLVMPointerType::get(LLVM::LLVMStructType::getIdentified(context, symbolStructName)),
        allocExprRef, barePtr.getResult());

    // Insert arguments into the symbols
    auto insertArgExprRef = getOrInsertAllocArgsSymbol(rewriter, parentModule);

    // Remove first operand: This is the function name pointer
    auto functionArguments = allocOp.getOperands().drop_front(1);

    // Push arguments for allocArguments call
    SmallVector<Value, 4> args;
    args.push_back(allocExprCall.getResult(0));
    auto numArgsValue = rewriter.create<LLVM::ConstantOp>(
        loc, LLVM::LLVMIntegerType::get(context, 64),
        IntegerAttr::get(IndexType::get(context), functionArguments.size()));
    args.push_back(numArgsValue.getResult());

    for(auto argument : functionArguments) {
      auto runtimeType = rewriter.create<LLVM::ConstantOp>(
          loc, LLVM::LLVMType::getInt64Ty(context),
          rewriter.getIntegerAttr(
              rewriter.getIndexType(),
              (int64_t)boss::mlir::conversion::mlirTypeToRuntimeType(argument.getType(), false)));

      // Extract memref
      if(argument.getType().isa<mlir::MemRefType>()) {
        auto basePointer = rewriter.create<LLVM::ExtractValueOp, Type, Value const&, ArrayAttr>(
            loc, LLVM::LLVMType::getInt8PtrTy(context), argument, rewriter.getI64ArrayAttr(0));
        args.push_back(basePointer);
      } else {
        args.push_back(argument);
      }

      args.push_back(runtimeType.getResult());
    }

    rewriter.create<LLVM::CallOp>(loc, LLVM::LLVMVoidType::get(context), insertArgExprRef, args);

    rewriter.replaceOp(allocOp.getOperation(), allocExprCall.getResults());
    return success();
  }

  TypeConverter& converter;
};

struct AppendToRelationOpLowering : public OpConversionPattern<database::AppendToRelationOp> {
  using OpConversionPattern::OpConversionPattern;

  template <boss::mlir::types::RuntimeTypes /*type*/>
  mlir::Operation* createAppendCall(ConversionPatternRewriter& rewriter /*rewriter*/, ModuleOp /*module*/ module,
                                       Value /*operand*/ operand, Location /*loc*/ loc,
                                       size_t /*relationBuilderPtr*/ relationBuilderPtr) const {
    return nullptr;
  }

  template <>
  mlir::Operation* createAppendCall<boss::mlir::types::RuntimeTypes::INT>(
      ConversionPatternRewriter& rewriter, ModuleOp module, Value operand, Location loc,
      size_t relationBuilderPtr) const {
    auto voidType = LLVM::LLVMVoidType::get(rewriter.getContext());
    auto intType = LLVM::LLVMIntegerType::get(rewriter.getContext(), 64);
    auto funcType = LLVM::LLVMFunctionType::get(
        voidType, {intType, LLVM::LLVMIntegerType::get(rewriter.getContext(), 32)});

    auto insertFunction = getOrInsertFunction("addToRelation_Int", funcType, rewriter, module);

    auto relationPtrConstant = rewriter.create<LLVM::ConstantOp>(
        loc, LLVM::LLVMType::getInt64Ty(rewriter.getContext()),
        rewriter.getIntegerAttr(rewriter.getIndexType(), relationBuilderPtr));

    ::mlir::ValueRange funcOperands{relationPtrConstant.getResult(), operand};

    auto funcOp = dyn_cast_or_null<LLVM::LLVMFuncOp>(module.lookupSymbol(insertFunction));

    if(!funcOp) {
      return nullptr;
    }

    return rewriter.create<LLVM::CallOp>(loc, funcOp, funcOperands);
  }

  LogicalResult matchAndRewrite(database::AppendToRelationOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();

    mlir::Operation* callOp = nullptr;
    switch((boss::mlir::types::RuntimeTypes)op.runtimeType()) {
    case boss::mlir::types::RuntimeTypes::INT:
      callOp = createAppendCall<boss::mlir::types::RuntimeTypes::INT>(
          rewriter, parentModule, operands.front(), op.getLoc(), op.relationBuilderPtr());
      break;
    case boss::mlir::types::RuntimeTypes::BOOLEAN:
      callOp = createAppendCall<boss::mlir::types::RuntimeTypes::BOOLEAN>(
          rewriter, parentModule, operands.front(), op.getLoc(), op.relationBuilderPtr());
      break;
    default:
      throw std::runtime_error("Type not currently implemented");
    }

    if(callOp == nullptr) {
      return failure();
    }

    rewriter.replaceOp(op, callOp->getResults());

    return success();
  }
};

struct FinalizeRelationOpLowering : public OpConversionPattern<database::FinalizeRelationOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::FinalizeRelationOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto intType = LLVM::LLVMIntegerType::get(op.getContext(), 64);
    auto funcType = LLVM::LLVMFunctionType::get(intType, {intType, intType});

    auto finalizeFunction = getOrInsertFunction("constructTable", funcType, rewriter, parentModule);

    auto relationPtrConstant = rewriter.create<LLVM::ConstantOp>(
        op.getLoc(), LLVM::LLVMType::getInt64Ty(rewriter.getContext()),
        rewriter.getIntegerAttr(rewriter.getIndexType(), op.relationBuildersPtr()));
    auto schemaPtr = rewriter.create<LLVM::ConstantOp>(
        op.getLoc(), LLVM::LLVMType::getInt64Ty(rewriter.getContext()),
        rewriter.getIntegerAttr(rewriter.getIndexType(), op.schemaPtr()));

    rewriter.replaceOpWithNewOp<LLVM::CallOp>(
        op, LLVM::LLVMIntegerType::get(rewriter.getContext(), 64), finalizeFunction,
        ::mlir::ValueRange{schemaPtr.getResult(), relationPtrConstant.getResult()});

    return success();
  }
};

void SexprToLLVMLoweringPass::runOnOperation() {
  LLVMConversionTarget target(getContext());
  target.addLegalOp<ModuleOp, ModuleTerminatorOp>();

  LowerToLLVMOptions options{};
  options.useBarePtrCallConv = true;

  LLVMTypeConverter typeConverter(&getContext(), options);

  // Insert void struct type
  //  LLVM::LLVMPointerType::createStructTy(&getContext(), voidStructName);

  // Insert S-Expression struct type
  auto symbolStruct = LLVM::LLVMStructType::createStructTy(&getContext(), symbolStructName);
  auto symbolArgumentStruct =
      LLVM::LLVMStructType::createStructTy(&getContext(), symbolArgStructName);

  // clang-format off
  // LLVM representation of struct ::Symbol
  symbolStruct.cast<LLVM::LLVMStructType>().setBody({
    LLVM::LLVMType::getInt8PtrTy(&getContext()),   // head pointer
    LLVM::LLVMIntegerType::get(&getContext(), 64), // argc
    LLVM::LLVMPointerType::get(                    // arguments
    LLVM::LLVMStructType::getIdentified(&getContext(), symbolArgStructName))
  }, false);
  // clang-format on

  // clang-format off
  // LLVM representation of struct ::SymbolArgument
  symbolArgumentStruct.cast<LLVM::LLVMStructType>().setBody({
    LLVM::LLVMType::getInt64Ty(&getContext()), // size_t integer to represent type
    LLVM::LLVMType::getInt64Ty(&getContext())  // size_t integer to represent value
  }, false);
  // clang-format on

  typeConverter.addConversion([](SymbolOrValueType t) -> llvm::Optional<Type> {
    if(t.isSymbolic() == sexprtype::SymbolOrValue::SYMBOL)
      return LLVM::LLVMPointerType::get(
          LLVM::LLVMStructType::getIdentified(t.getContext(), symbolStructName));
    return llvm::Optional<Type>{};
  });

  typeConverter.addConversion(
      [&typeConverter](TupleStreamType t,
                       SmallVectorImpl<Type>& result) -> llvm::Optional<LogicalResult> {
        for(auto const& nameAndType : t.getTupleTypes()) {
          result.push_back(typeConverter.convertType(nameAndType.second));
        }
        return success();
      });

  typeConverter.addConversion([](RelationType t) -> llvm::Optional<Type> {
    // TODO change to correct type
    return LLVM::LLVMIntegerType::get(t.getContext(), 32);
  });

  OwningRewritePatternList patterns;
  populateLoopToStdConversionPatterns(patterns, &getContext());
  populateStdToLLVMConversionPatterns(typeConverter, patterns);

  patterns.insert<PrintMemrefOpLowering, AllocateSymbolOpLowering,
                  AllocateSymbolicFunctionOpLowering, LoadConstantAddressOpLowering>(&getContext(),
                                                                                     typeConverter);

  patterns.insert<FinalizeRelationOpLowering, AppendToRelationOpLowering>(&getContext());

  auto module = getOperation();
  //
  //  printMutex.lock();
  //  module.dump();
  //  printMutex.unlock();

  if(failed(applyFullConversion(module, target, std::move(patterns)))) {
    signalPassFailure();
  }
}

// namespace
}; // namespace

std::unique_ptr<mlir::Pass> createLowerToLLVMPass(runtime::Database& database) {
  return std::make_unique<SexprToLLVMLoweringPass>(database);
}
