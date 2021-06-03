#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/HashAggregate.hpp"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
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

static const StringRef symbolStructName{"SymbolStruct"};
static const StringRef symbolArgStructName{"SymbolArgStruct"};
static const StringRef stringStructName{"RuntimeStringStruct"};
static const StringRef voidStructName{"VoidStruct"};

LLVM::LLVMType getRuntimeStringStructType(MLIRContext* context) {
  auto type = LLVM::LLVMStructType::getIdentified(context, stringStructName);
  type.setBody({LLVM::LLVMPointerType::get(LLVM::LLVMIntegerType::get(context, 8)),
                LLVM::LLVMIntegerType::get(context, 64)},
               false);

  return type;
}

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
  auto argType = LLVM::LLVMPointerType::get(getRuntimeStringStructType(rewriter.getContext()));
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

  SexprToLLVMLoweringPass(new_runtime::Database& database) : database(database){};

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }
  void runOnOperation() final;

  new_runtime::Database& database;
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

    auto ptrType =
        LLVM::LLVMPointerType::get(converter.convertType(op.getType()).cast<LLVM::LLVMType>());

    auto basePtr =
        rewriter.create<LLVM::IntToPtrOp>(op.getLoc(), ptrType, bufferAddress.getResult());

    auto effectivePtr =
        rewriter.create<LLVM::GEPOp>(op.getLoc(), ptrType, basePtr.getResult(), operands.front());

    rewriter.replaceOpWithNewOp<LLVM::LoadOp>(op, effectivePtr);

    return success();
  }

  TypeConverter& converter;
};

struct StringCompareOpLowering : public OpConversionPattern<memory::StringCompareOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(memory::StringCompareOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto loc = op.getLoc();
    auto context = op.getContext();
    ModuleOp parentModule = op.getParentOfType<ModuleOp>();

    auto stringStructType = getRuntimeStringStructType(context);
    auto stringStructPtr = LLVM::LLVMPointerType::get(stringStructType);
    auto boolType = LLVM::LLVMIntegerType::get(op.getContext(), 1);

    auto compareFuncType = LLVM::LLVMFunctionType::get(boolType, {stringStructPtr, stringStructPtr});
    auto compareFunc = getOrInsertFunction("runtimeStringCompare", compareFuncType, rewriter, parentModule);

    auto compareResult = rewriter.create<CallOp>(op.getLoc(), compareFunc, boolType, ValueRange{operands[0], operands[1]});

    rewriter.replaceOp(op, compareResult.getResults());
    return success();
  }
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

    auto allocExprCall = rewriter.create<LLVM::CallOp>(
        loc,
        LLVM::LLVMPointerType::get(LLVM::LLVMStructType::getIdentified(context, symbolStructName)),
        allocExprRef, allocOp.name());

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

    auto allocExprCall = rewriter.create<LLVM::CallOp>(
        loc,
        LLVM::LLVMPointerType::get(LLVM::LLVMStructType::getIdentified(context, symbolStructName)),
        allocExprRef, allocOp.name());

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
      args.push_back(argument);
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
  mlir::Operation* createAppendCall(ConversionPatternRewriter& rewriter /*rewriter*/,
                                    ModuleOp /*module*/ module, Value /*operand*/ operand,
                                    Location /*loc*/ loc,
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

  template <>
  mlir::Operation* createAppendCall<boss::mlir::types::RuntimeTypes::FLOAT>(
      ConversionPatternRewriter& rewriter, ModuleOp module, Value operand, Location loc,
      size_t relationBuilderPtr) const {
    auto voidType = LLVM::LLVMVoidType::get(rewriter.getContext());
    auto intType = LLVM::LLVMIntegerType::get(rewriter.getContext(), 64);
    auto funcType = LLVM::LLVMFunctionType::get(
        voidType, {intType, LLVM::LLVMFloatType::get(rewriter.getContext())});

    auto insertFunction = getOrInsertFunction("addToRelation_Float", funcType, rewriter, module);

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

  template <>
  mlir::Operation* createAppendCall<boss::mlir::types::RuntimeTypes::STRING>(
      ConversionPatternRewriter& rewriter, ModuleOp module, Value operand, Location loc,
      size_t relationBuilderPtr) const {

    auto voidType = LLVM::LLVMVoidType::get(rewriter.getContext());
    auto intType = LLVM::LLVMIntegerType::get(rewriter.getContext(), 64);
    auto stringPtrType = LLVM::LLVMPointerType::get(getRuntimeStringStructType(rewriter.getContext()));
    auto funcType = LLVM::LLVMFunctionType::get(
        voidType, {intType, stringPtrType});

    auto insertFunction = getOrInsertFunction("addToRelation_String", funcType, rewriter, module);

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

    auto runtimeType = (boss::mlir::types::RuntimeTypes)op.runtimeType();

    // get reference to correct function call
    mlir::Operation* callOp = nullptr;
    switch(runtimeType) {
    case boss::mlir::types::RuntimeTypes::INT:
      callOp = createAppendCall<boss::mlir::types::RuntimeTypes::INT>(
          rewriter, parentModule, operands.front(), op.getLoc(), op.relationBuilderPtr());
      break;
    case boss::mlir::types::RuntimeTypes::BOOLEAN:
      callOp = createAppendCall<boss::mlir::types::RuntimeTypes::BOOLEAN>(
          rewriter, parentModule, operands.front(), op.getLoc(), op.relationBuilderPtr());
      break;
    case boss::mlir::types::RuntimeTypes::FLOAT:
      callOp = createAppendCall<boss::mlir::types::RuntimeTypes::FLOAT>(
          rewriter, parentModule, operands.front(), op.getLoc(), op.relationBuilderPtr());
      break;
    case boss::mlir::types::RuntimeTypes::STRING:
      callOp = createAppendCall<boss::mlir::types::RuntimeTypes::STRING>(
          rewriter, parentModule, operands.front(), op.getLoc(), op.relationBuilderPtr());
      break;
    default:
      throw std::runtime_error("Type not currently implemented: " + std::to_string(op.runtimeType()));
    }

    if(callOp == nullptr) {
      return failure();
    }

    // Insert function call
    rewriter.replaceOp(op, callOp->getResults());

    return success();
  }
};

struct FinalizeBuilderOpLowering : public OpConversionPattern<database::FinalizeBuilderOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::FinalizeBuilderOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto intType = LLVM::LLVMIntegerType::get(op.getContext(), 64);
    auto funcType = LLVM::LLVMFunctionType::get(intType, {intType});

    auto finalizeFunction =
        getOrInsertFunction(op.builderFunctionName().str(), funcType, rewriter, parentModule);

    auto relationPtrConstant = rewriter.create<LLVM::ConstantOp>(
        op.getLoc(), LLVM::LLVMType::getInt64Ty(rewriter.getContext()),
        rewriter.getIntegerAttr(rewriter.getIndexType(), op.builderPtr()));

    rewriter.replaceOpWithNewOp<LLVM::CallOp>(
        op, LLVM::LLVMIntegerType::get(rewriter.getContext(), 64), finalizeFunction,
        ::mlir::ValueRange{relationPtrConstant.getResult()});

    return success();
  }
};

struct HashFindOpLowering : public OpConversionPattern<database::FindInHashTableOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::FindInHashTableOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto hash = operands[0];

    auto intType = LLVM::LLVMIntegerType::get(rewriter.getContext(), 64);
    auto funcType = LLVM::LLVMFunctionType::get(intType, {intType, intType});

    auto lookupFunction = getOrInsertFunction("hashTableLookup", funcType, rewriter, parentModule);

    auto tablePtr = op.tablePtr();

    auto mapPointer = rewriter.create<ConstantIndexOp>(op.getLoc(), tablePtr);

    rewriter.replaceOpWithNewOp<CallOp>(op, lookupFunction, intType, ValueRange{mapPointer, hash});

    return success();
  }
};

struct InsertHashtableOpLowering : public OpConversionPattern<database::InsertIntoHashTableOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::InsertIntoHashTableOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto hash = operands[0];
    auto offsetPointer = operands[1];

    auto voidType = LLVM::LLVMVoidType::get(rewriter.getContext());
    auto intType = LLVM::LLVMIntegerType::get(rewriter.getContext(), 64);
    auto funcType = LLVM::LLVMFunctionType::get(voidType, {intType, intType, intType, intType});

    auto insertionFunction =
        getOrInsertFunction("hashTableInsert", funcType, rewriter, parentModule);

    auto childIndex = rewriter.create<ConstantIndexOp>(op.getLoc(), op.rightTupleStreamIndex());
    auto tablePtr = rewriter.create<ConstantIndexOp>(op.getLoc(), op.tablePtr());

    rewriter.create<CallOp>(op.getLoc(), insertionFunction, voidType,
                            ValueRange{tablePtr, childIndex, hash, offsetPointer});
    rewriter.eraseOp(op);

    return success();
  }
};

struct GroupByGetLowering : public OpConversionPattern<database::GroupByGet> {
  GroupByGetLowering(MLIRContext* context, LLVMTypeConverter& converter)
      : converter(converter), OpConversionPattern(context) {}

  LogicalResult matchAndRewrite(database::GroupByGet op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto context = op.getContext();
    auto loc = op.getLoc();

    auto hash = operands[0];
    auto tablePtr = op.hashAggregatePointer().getLimitedValue();
    auto* table = reinterpret_cast<runtime::aggregate::HashAggregate*>(tablePtr);
    auto aggregateType = table->getAggregateType();

    auto tablePtrValue = rewriter.create<ConstantIndexOp>(loc, tablePtr);

    auto extractionType = converter.convertType(op.getType()).dyn_cast<LLVM::LLVMType>();

    std::string funcName;
    // TODO remaining types
    switch(aggregateType) {
    case boss::mlir::types::RuntimeTypes::INT:
      funcName = "groupByGet_Int";
      break;
    }

    auto intType = LLVM::LLVMIntegerType::get(context, 64);
    auto funcType = LLVM::LLVMFunctionType::get(extractionType, {intType, intType});

    auto extractionFunction = getOrInsertFunction(funcName, funcType, rewriter, parentModule);

    rewriter.replaceOpWithNewOp<CallOp>(op, extractionFunction, extractionType,
                                        ValueRange{hash, tablePtrValue.getResult()});

    return success();
  }

  LLVMTypeConverter& converter;
};

struct GroupByInsertLowering : public OpConversionPattern<database::GroupByInsert> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::GroupByInsert op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto context = op.getContext();
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto hash = operands[0];
    auto value = operands[1];
    auto tablePtr = op.hashAggregatePointer().getLimitedValue();
    auto* table = reinterpret_cast<runtime::aggregate::HashAggregate*>(tablePtr);

    auto tablePtrValue = rewriter.create<ConstantIndexOp>(op.getLoc(), tablePtr);

    auto voidType = LLVM::LLVMVoidType::get(rewriter.getContext());
    auto intType = LLVM::LLVMIntegerType::get(rewriter.getContext(), 64);

    auto valueType = table->getAggregateType();
    std::string funcName;
    LLVM::LLVMType argType;
    switch(valueType) {
      // TODO other types
    case boss::mlir::types::RuntimeTypes::INT:
      funcName = "groupByInsert_Int";
      argType = LLVM::LLVMIntegerType::get(context, 32);
      break;
    }

    auto funcType = LLVM::LLVMFunctionType::get(voidType, {argType, intType, intType});

    auto insertionFunction = getOrInsertFunction(funcName, funcType, rewriter, parentModule);

    rewriter.create<CallOp>(op.getLoc(), insertionFunction, voidType,
                            ValueRange{value, hash, tablePtrValue.getResult()});
    rewriter.eraseOp(op);

    return success();
  }
};

struct AllocateStringOpLowering : public OpConversionPattern<memory::AllocateStringOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(memory::AllocateStringOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto* context = op.getContext();
    auto stringPtr = LLVM::LLVMPointerType::get(getRuntimeStringStructType(context));
    auto indexType = LLVM::LLVMIntegerType::get(context, 64);

    auto funcType = LLVM::LLVMFunctionType::get(stringPtr, {indexType});
    auto allocString =
        getOrInsertFunction("allocateRuntimeString", funcType, rewriter, parentModule);

    auto allocCall = rewriter.create<CallOp>(op.getLoc(), allocString, funcType.getReturnType(),
                                             ValueRange{operands[0]});

    rewriter.replaceOp(op, allocCall.getResult(0));
    op.replaceAllUsesWith(allocCall.getResult(0));
    return success();
  }
};

struct StringLengthOpLowering : public OpConversionPattern<memory::StringLengthOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(memory::StringLengthOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto loc = op.getLoc();
    auto context = op.getContext();
    auto stringStructType = getRuntimeStringStructType(context);

    auto stringStruct = rewriter.create<LLVM::LoadOp>(loc, stringStructType, operands[0]);
    auto stringLen = rewriter.create<LLVM::ExtractValueOp>(
        loc, LLVM::LLVMIntegerType::get(context, 64), stringStruct.getResult(),
        rewriter.getArrayAttr({rewriter.getIndexAttr(1)}));
    rewriter.replaceOp(op, stringLen.getResult());
    return success();
  }
};

struct StringCopyOpLowering : public OpConversionPattern<memory::StringCopyOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(memory::StringCopyOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto loc = op.getLoc();
    auto context = op.getContext();
    auto parentModule = op.getParentOfType<ModuleOp>();

    // Get all types
    auto stringStructType = getRuntimeStringStructType(context);
    auto charPtrType = LLVM::LLVMPointerType::get(LLVM::LLVMIntegerType::get(context, 8));
    auto sizeType = LLVM::LLVMIntegerType::get(context, 64);
    auto voidType = LLVM::LLVMVoidType::get(context);
    auto memcpyType = LLVM::LLVMFunctionType::get(voidType, {charPtrType, charPtrType, sizeType});
    auto memcpyFunc = getOrInsertFunction("memcpy", memcpyType, rewriter, parentModule);

    // Load src and dest raw pointers
    auto firstStructField = rewriter.getArrayAttr({rewriter.getIndexAttr(0)});
    auto secondStructField = rewriter.getArrayAttr({rewriter.getIndexAttr(1)});
    auto sourceStruct = rewriter.create<LLVM::LoadOp>(loc, stringStructType, op.source());
    auto sourcePtr = rewriter.create<LLVM::ExtractValueOp>(
        loc, charPtrType, sourceStruct.getResult(), firstStructField);
    auto sourceLen = rewriter.create<LLVM::ExtractValueOp>(loc, sizeType, sourceStruct.getResult(),
                                                           secondStructField);
    auto destStruct = rewriter.create<LLVM::LoadOp>(loc, stringStructType, op.dest());
    auto destPtr = rewriter.create<LLVM::ExtractValueOp>(loc, charPtrType, destStruct.getResult(),
                                                         firstStructField);

    // Compute destination pointer with offset
    auto destPtrAsInt = rewriter.create<LLVM::PtrToIntOp>(loc, sizeType, destPtr.getResult());
    auto destWithOffsetInt = rewriter.create<LLVM::AddOp>(loc, destPtrAsInt, op.offset());
    auto destPtrWithOffset = rewriter.create<LLVM::IntToPtrOp>(loc, charPtrType, destWithOffsetInt);

    // Call copy function
    rewriter.create<CallOp>(loc, memcpyFunc, voidType,
                            ValueRange{destPtrWithOffset, sourcePtr, sourceLen});
    rewriter.eraseOp(op);

    return success();
  }
};

struct StringReferenceOpLowering : public OpConversionPattern<memory::StringReferenceOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(memory::StringReferenceOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto loc = op.getLoc();
    auto context = op.getContext();
    auto parentModule = op.getParentOfType<ModuleOp>();

    // Get types
    auto stringStructType = getRuntimeStringStructType(context);
    auto stringStructPtrType = LLVM::LLVMPointerType::get(stringStructType);
    auto bytePointer = LLVM::LLVMPointerType::get(LLVM::LLVMIntegerType::get(context, 8));
    auto sizeType = LLVM::LLVMIntegerType::get(context, 64);
    auto allocStringRefType =
        LLVM::LLVMFunctionType::get(stringStructPtrType, {bytePointer, sizeType});
    auto allocStringRefFunc = getOrInsertFunction("allocateRuntimeStringReference",
                                                  allocStringRefType, rewriter, parentModule);

    // Create the memory for the struct holding the string pointer and length
    auto stringDataPtr = rewriter.create<LLVM::IntToPtrOp>(loc, bytePointer, op.pointer());
    auto stringStructMem =
        rewriter.create<CallOp>(loc, allocStringRefFunc, stringStructPtrType,
                                ValueRange{stringDataPtr.getResult(), op.length()});
    // TODO create a way to re-use references

    rewriter.replaceOp(op, stringStructMem.getResults());
    return success();
  }
};

struct HashValuesOpLowering : public OpConversionPattern<database::HashValuesOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::HashValuesOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto* context = op.getContext();

    // Hash all of the arguments
    std::stack<Value> hashedValues;
    for(auto const& arg : op.getOperands()) {
      auto const& argType = arg.getType();

      auto runtimeType = boss::mlir::conversion::mlirTypeToRuntimeType(argType, false);

      std::string hashFuncName;
      LLVM::LLVMType argumentType;

      // Hash value
      // TODO handle remaining types
      switch(runtimeType) {
      case boss::mlir::types::RuntimeTypes::STRING:
        hashFuncName = "hash_String";
        argumentType = LLVM::LLVMPointerType::get(getRuntimeStringStructType(context));
        break;
      case boss::mlir::types::RuntimeTypes::INT:
        hashFuncName = "hash_Int";
        argumentType = LLVM::LLVMIntegerType::get(context, 32);
        break;
      case boss::mlir::types::RuntimeTypes::BOOLEAN:
        hashFuncName = "hash_Boolean";
        argumentType = LLVM::LLVMIntegerType::get(context, 1);
        break;
      case boss::mlir::types::RuntimeTypes::FLOAT:
        hashFuncName = "hash_Float";
        argumentType = LLVM::LLVMFloatType::get(context);
        break;
      }

      auto intType = LLVM::LLVMIntegerType::get(op.getContext(), 64);
      auto hashFunctionType = LLVM::LLVMFunctionType::get(intType, {argumentType});
      auto hashFunction =
          getOrInsertFunction(hashFuncName, hashFunctionType, rewriter, parentModule);
      auto hashedValue = rewriter.create<CallOp>(op.getLoc(), hashFunction,
                                                 hashFunctionType.getReturnType(), ValueRange{arg});

      hashedValues.push(hashedValue.getResult(0));
    }

    // Hash all the hashed arguments into a single value
    while(hashedValues.size() > 1) {
      auto leftValue = hashedValues.top();
      hashedValues.pop();
      auto rightValue = hashedValues.top();
      hashedValues.pop();

      auto intType = LLVM::LLVMIntegerType::get(op.getContext(), 64);
      auto hashFunctionType = LLVM::LLVMFunctionType::get(intType, {intType, intType});
      auto hashFunction =
          getOrInsertFunction("hash_Combine", hashFunctionType, rewriter, parentModule);
      auto hashedValue =
          rewriter.create<CallOp>(op.getLoc(), hashFunction, hashFunctionType.getReturnType(),
                                  ValueRange{leftValue, rightValue});
      hashedValues.push(hashedValue.getResult(0));
    }

    // Return the hashed value
    rewriter.replaceOp(op, hashedValues.top());
    return success();
  }
};

struct AdvanceBuilderOpLowering : public OpConversionPattern<database::AdvanceBuilderOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(database::AdvanceBuilderOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();
    auto int64Type = LLVM::LLVMIntegerType::get(op.getContext(), 64);
    auto int8Type = LLVM::LLVMIntegerType::get(op.getContext(), 8);
    auto funcType = LLVM::LLVMFunctionType::get(int64Type, {int64Type, int64Type, int8Type});

    auto advanceFunction = getOrInsertFunction("advanceBuilder", funcType, rewriter, parentModule);

    auto structPtrConst = rewriter.create<LLVM::ConstantOp>(
        op.getLoc(), LLVM::LLVMType::getInt64Ty(rewriter.getContext()),
        rewriter.getIntegerAttr(rewriter.getIndexType(), op.structBuilderPtr()));
    auto unionPtrConst = rewriter.create<LLVM::ConstantOp>(
        op.getLoc(), LLVM::LLVMType::getInt64Ty(rewriter.getContext()),
        rewriter.getIntegerAttr(rewriter.getIndexType(), op.unionBuilderPtr()));
    auto childIndexConst = rewriter.create<LLVM::ConstantOp>(
        op.getLoc(), LLVM::LLVMType::getInt8Ty(rewriter.getContext()),
        rewriter.getIntegerAttr(rewriter.getIndexType(), op.childId()));

    auto funcOp = dyn_cast_or_null<LLVM::LLVMFuncOp>(parentModule.lookupSymbol(advanceFunction));
    if(!funcOp) {
      return failure();
    }

    rewriter.replaceOpWithNewOp<LLVM::CallOp>(op, funcOp,
                                              ::mlir::ValueRange{structPtrConst.getResult(),
                                                                 unionPtrConst.getResult(),
                                                                 childIndexConst.getResult()});

    return success();
  }
};

struct LoadArrayIndirectOpLowering : public OpConversionPattern<database::LoadArrayIndirectOp> {
  LoadArrayIndirectOpLowering(MLIRContext* context, LLVMTypeConverter& converter)
      : OpConversionPattern(context), converter(converter) {}

  LogicalResult matchAndRewrite(database::LoadArrayIndirectOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter& rewriter) const override {
    auto parentModule = op.getParentOfType<ModuleOp>();

    auto resultType = converter.convertType(op.getType()).dyn_cast<LLVM::LLVMType>();

    auto indexType = LLVM::LLVMIntegerType::get(op.getContext(), 64);
    auto funcType =
        LLVM::LLVMFunctionType::get(resultType, {indexType, indexType, indexType, indexType});

    // TODO different func depending on type
    auto loadFunction = getOrInsertFunction("loadIndirect_Int", funcType, rewriter, parentModule);

    auto databasePtr =
        rewriter.create<ConstantIndexOp>(op.getLoc(), op.databasePtr().getLimitedValue());
    auto colIndex =
        rewriter.create<ConstantIndexOp>(op.getLoc(), op.columnIndex().getLimitedValue());

    auto callOp = rewriter.create<CallOp>(
        op.getLoc(), loadFunction, funcType.getReturnType(),
        ValueRange{databasePtr.getResult(), colIndex.getResult(), op.typeId(), op.valueOffset()});

    rewriter.replaceOp(op, callOp.getResult(0));

    return success();
  }

  LLVMTypeConverter& converter;
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

  // Insert RuntimeString struct type
  auto runtimeStringStruct = getRuntimeStringStructType(&getContext());

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

  // clang-format off
//  runtimeStringStruct.cast<LLVM::LLVMStructType>().setBody(, false);
  // clang-format on

  typeConverter.addConversion([](SymbolOrValueType t) -> llvm::Optional<Type> {
    if(t.isSymbolic() == sexprtype::SymbolOrValue::SYMBOL)
      return LLVM::LLVMPointerType::get(
          LLVM::LLVMStructType::getIdentified(t.getContext(), symbolStructName));
    return llvm::Optional<Type>{};
  });

  typeConverter.addConversion([](RelationType t) -> llvm::Optional<Type> {
    // TODO change to correct type
    return LLVM::LLVMIntegerType::get(t.getContext(), 32);
  });

  typeConverter.addConversion(
      [&](StringType t) { return LLVM::LLVMPointerType::get(runtimeStringStruct); });

  OwningRewritePatternList patterns;
  populateLoopToStdConversionPatterns(patterns, &getContext());
  populateStdToLLVMConversionPatterns(typeConverter, patterns);

  patterns
      .insert<PrintMemrefOpLowering, AllocateSymbolOpLowering, AllocateSymbolicFunctionOpLowering,
              LoadConstantAddressOpLowering, GroupByGetLowering, LoadArrayIndirectOpLowering>(
          &getContext(), typeConverter);

  patterns.insert<FinalizeBuilderOpLowering, AppendToRelationOpLowering, AdvanceBuilderOpLowering,
                  HashValuesOpLowering, InsertHashtableOpLowering, HashFindOpLowering,
                  GroupByInsertLowering, AllocateStringOpLowering, StringReferenceOpLowering,
                  StringCopyOpLowering, StringLengthOpLowering, StringCompareOpLowering>(&getContext());

  auto module = getOperation();

  if(failed(applyFullConversion(module, target, std::move(patterns)))) {
    signalPassFailure();
  }
}

// namespace
}; // namespace

std::unique_ptr<mlir::Pass> createLowerToLLVMPass(new_runtime::Database& database) {
  return std::make_unique<SexprToLLVMLoweringPass>(database);
}
