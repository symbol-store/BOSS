#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseDialect.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryDialect.h"
#include "Engines/MLIREngine/Dialect/MemoryDialect/MemoryOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/Runtime/HashTable.hpp"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include "SexprToStd.hpp"
#include "Utilities.hpp"
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

    auto newFuncType = rewriter.getFunctionType(convertedInputs, convertedResults);

    rewriter.updateRootInPlace(op, [&]() {
      op.setType(newFuncType);
      auto status = rewriter.convertRegionTypes(&op.getBody(), converter);
      if(failed(status)) {
        throw std::runtime_error("Failure converting block arguments");
      }
    });

    return success();
  }

  TypeConverter& converter;
};

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
    // TODO make it work for strings
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
        {"And", [&]() { return replaceBinaryOp<mlir::AndOp>(s, rewriter); }},
        {"Eval",
         [&]() {
           rewriter.replaceOp(s.getOperation(), s.getOperands());
           return success();
         }},
        {"StringJoin", [&]() { return rewriteStringJoin(s, operands, rewriter); }},
        {"Greater",
         [&]() { return replaceBooleanCompareOp<CmpIPredicate::sgt>(s, operands, rewriter); }},
        {"Less",
         [&]() { return replaceBooleanCompareOp<CmpIPredicate::slt>(s, operands, rewriter); }},
        {"Eq", [&]() { return replaceBooleanCompareOp<CmpIPredicate::eq>(s, operands, rewriter); }},
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
        {"Select",
         [&]() {
           auto tupleStreamUnion = converter.convertType(operands[1].getType())
                                       .dyn_cast_or_null<TupleStreamUnionType>();

           auto* whereClause = s.getOperands()[0].getDefiningOp();
           auto fields = whereClause->getAttr("fields").dyn_cast_or_null<ArrayAttr>();
           auto filterFunctions = whereClause->getAttr("functions").dyn_cast_or_null<ArrayAttr>();

           if(!tupleStreamUnion || !filterFunctions || !fields) {
             return failure();
           }

           std::vector<mlir::Value> tupleStreamValues;
           for(auto i = 0UL; i < tupleStreamUnion.getNumChildStreams(); i++) {
             auto tupleStreamTy = tupleStreamUnion.getTupleStreams()[i];
             auto extractionOp = rewriter.create<database::GetTupleStreamFromUnion>(
                 s.getLoc(), tupleStreamTy, operands[1], i);

             auto selectionOp = rewriter.create<database::SelectionOp>(
                 s.getLoc(), extractionOp.getResult(),
                 filterFunctions[i].dyn_cast_or_null<StringAttr>(), fields);
             tupleStreamValues.emplace_back(selectionOp.getResult());
           }

           rewriter.replaceOpWithNewOp<database::CreateUnionTupleStream>(s, tupleStreamUnion,
                                                                         tupleStreamValues);

           return success();
         }},
        {"GroupBy",
         [&]() {
           auto tupleStreamUnion = converter.convertType(operands[2].getType())
                                       .dyn_cast_or_null<TupleStreamUnionType>();

           auto* context = s.getContext();

           // Get the fields that are required for the aggregation function
           auto lambda = s.getOperands()[1].getDefiningOp();
           auto aggrFields = lambda->getAttr("fields").dyn_cast_or_null<ArrayAttr>();
           auto funcs = lambda->getAttr("functions").dyn_cast_or_null<ArrayAttr>();

           if(!lambda || !aggrFields || !funcs) {
             return failure();
           }

           // Get the fields that are required for the grouping
           auto groupFields = s.getOperand(0).getDefiningOp<sexpr::SymbolOp>();
           std::vector<StringRef> groupingFields;
           for(auto const& arg : groupFields.getOperands()) {
             groupingFields.emplace_back(arg.getDefiningOp<sexpr::StringConstantOp>().value());
           }
           auto grpFieldsAttr = rewriter.getStrArrayAttr(groupingFields);

           std::vector<mlir::Value> tupleStreamValues;
           for(auto i = 0UL; i < tupleStreamUnion.getNumChildStreams(); i++) {
             auto tupleStreamTy = tupleStreamUnion.getTupleStreams()[i];
             auto extractionOp = rewriter.create<database::GetTupleStreamFromUnion>(
                 s.getLoc(), tupleStreamTy, operands[2], i);
             tupleStreamValues.emplace_back(extractionOp.getResult());
           }

           rewriter.replaceOpWithNewOp<database::GroupByOp>(
               s, RelationType::get(context), tupleStreamValues, grpFieldsAttr, aggrFields, funcs);

           return success();
         }},
        {"Project",
         [&]() {
           auto inputStreamUnion = converter.convertType(operands[1].getType())
                                       .dyn_cast_or_null<TupleStreamUnionType>();

           auto outputStreamUnion = converter.convertType(s.getResult().getType())
                                        .dyn_cast_or_null<TupleStreamUnionType>();

           if(!inputStreamUnion || !outputStreamUnion) {
             s.emitError("Failure casting types");
             return failure();
           }

           std::vector<mlir::Value> tupleStreamValues;
           for(auto i = 0U; i < inputStreamUnion.getNumChildStreams(); i++) {
             auto inputTupleStream = inputStreamUnion.getTupleStreams()[i];
             auto outputTupleStream = outputStreamUnion.getTupleStreams()[i];

             auto extractionOp = rewriter.create<database::GetTupleStreamFromUnion>(
                 s.getLoc(), inputTupleStream, operands[1], i);

             auto projectionResult = rewriter.create<database::ProjectionOp>(
                 s.getLoc(), outputTupleStream, extractionOp.getResult());

             tupleStreamValues.emplace_back(projectionResult.getResult());
           }

           rewriter.replaceOpWithNewOp<database::CreateUnionTupleStream>(s, outputStreamUnion,
                                                                         tupleStreamValues);

           return success();
         }},
        {"BuildHashTable",
         [&]() {
           auto inputStreamUnion = converter.convertType(operands[1].getType())
                                       .dyn_cast_or_null<TupleStreamUnionType>();

           auto* hashTable = new runtime::hash::HashTable{inputStreamUnion.getNumChildStreams()};
           auto hashTablePtr = reinterpret_cast<size_t>(hashTable);

           // Get the fields to join on
           std::vector<Attribute> rightJoinFields;
           // TODO create some general constant propagation mechanism
           for(auto const& stringPair : s.getOperand(0).getDefiningOp()->getOperands()) {
             auto pair = mlir::dyn_cast_or_null<sexpr::SymbolOp>(stringPair.getDefiningOp());

             auto rightStr = mlir::dyn_cast_or_null<sexpr::StringConstantOp>(
                 pair.getOperand(1).getDefiningOp());

             rightJoinFields.emplace_back(rewriter.getStringAttr(rightStr.value()));
           }
           auto rightFields = rewriter.getArrayAttr(rightJoinFields);

           // Create insert phase
           std::vector<Value> rightTupleStreams;
           for(auto i = 0U; i < inputStreamUnion.getNumChildStreams(); i++) {
             auto tupleStreamTy = inputStreamUnion.getTupleStreams()[i];
             auto extractionOp = rewriter.create<database::GetTupleStreamFromUnion>(
                 s.getLoc(), tupleStreamTy, operands[1], i);
             rightTupleStreams.emplace_back(extractionOp.getResult());
           }

           rewriter.create<database::BuildJoinTableOp>(s.getLoc(), ValueRange(rightTupleStreams),
                                                       rightFields, hashTablePtr);

           rewriter.replaceOpWithNewOp<ConstantIndexOp>(s, hashTablePtr);
           return success();
         }},
        {"Join",
         [&]() {
           auto leftInputStreamUnion = converter.convertType(operands[1].getType())
                                           .dyn_cast_or_null<TupleStreamUnionType>();

           auto outputStreamUnion =
               converter.convertType(s.getType()).dyn_cast_or_null<TupleStreamUnionType>();

           auto hashTablePtr = s.getOperand(2).getDefiningOp<sexpr::IntegerConstantOp>().value();
           auto* hashTable = reinterpret_cast<runtime::hash::HashTable*>(hashTablePtr);

           if(!leftInputStreamUnion || !outputStreamUnion) {
             s.emitError("Expecting tuple streams");
             return failure();
           }

           // Get the fields to join on
           std::vector<Attribute> leftJoinFields;
           std::vector<Attribute> rightJoinFields;
           // TODO create some general constant propagation mechanism
           for(auto const& stringPair : s.getOperand(0).getDefiningOp()->getOperands()) {
             auto pair = mlir::dyn_cast_or_null<sexpr::SymbolOp>(stringPair.getDefiningOp());

             auto leftStr = mlir::dyn_cast_or_null<sexpr::StringConstantOp>(
                 pair.getOperand(0).getDefiningOp());
             auto rightStr = mlir::dyn_cast_or_null<sexpr::StringConstantOp>(
                 pair.getOperand(1).getDefiningOp());

             leftJoinFields.emplace_back(rewriter.getStringAttr(leftStr.value()));
             rightJoinFields.emplace_back(rewriter.getStringAttr(rightStr.value()));
           }
           auto leftFields = rewriter.getArrayAttr(leftJoinFields);
           auto rightFields = rewriter.getArrayAttr(rightJoinFields);

           std::vector<Value> resultTupleStreams;

           // Create lookup phase for each partition
           for(auto i = 0U; i < leftInputStreamUnion.getNumChildStreams(); i++) {
             auto leftStreamTy = leftInputStreamUnion.getTupleStreams()[i];
             auto extractionOp = rewriter.create<database::GetTupleStreamFromUnion>(
                 s.getLoc(), leftStreamTy, s.getOperand(1), i);

             for(auto j = 0U; j < hashTable->getNumChildArrays(); j++) {
               auto hashTableFields = hashTable->getChildFields(j);
               // Check that the types match on the join fields - only join if they do
               // eg. (Join (On (Pair "A" "B"))) - only join partitions where A, B have same type
               auto typesMatch = true;
               for(auto k = 0U; k < leftJoinFields.size(); k++) {
                 auto leftField = leftJoinFields[k].dyn_cast_or_null<StringAttr>().getValue();
                 auto rightField = rightJoinFields[k].dyn_cast_or_null<StringAttr>().getValue();

                 auto leftType = leftStreamTy.getFields().find(leftField.str())->second;
                 auto rightType = boss::mlir::conversion::arrowTypeToMLIRType(
                     s.getContext(), hashTableFields.find(rightField.str())->second);

                 if(leftType != rightType) {
                   typesMatch = false;
                   break;
                 }
               }

               // If the types do match, then output a lookup op for these two partitions
               if(typesMatch) {
                 auto type =
                     outputStreamUnion
                         .getTupleStreams()[i * leftInputStreamUnion.getNumChildStreams() + j];
                 auto lookupOp = rewriter.create<database::LookupJoinOp>(
                     s.getLoc(), extractionOp.getResult(), hashTablePtr, j, type, leftFields,
                     rightFields);
                 resultTupleStreams.emplace_back(lookupOp.getResult());
               }
             }
           }

           rewriter.replaceOpWithNewOp<database::CreateUnionTupleStream>(s, outputStreamUnion,
                                                                         resultTupleStreams);

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

  c.addConversion([](Type type) { return type; });

  populateSymbolToStdPatterns(patterns, c, &getContext());

  // Register legality of dialects and operations
  target.addLegalDialect<mlir::StandardOpsDialect, mlir::scf::SCFDialect,
                         mlir::memory::MemoryDialect, database::DatabaseDialect>();
  target.addIllegalDialect<sexpr::SExprDialect>();
  target.addDynamicallyLegalOp<FuncOp>(
      [&](mlir::FuncOp op) { return c.isSignatureLegal(op.getType()); });
  target.addDynamicallyLegalOp<mlir::CallOp>(
      // CallOp is legal in source and target, but the type should be converted
      [&](mlir::CallOp op) { return c.isLegal(op); });

  patterns.insert<CallOpSignatureConversion, FuncOpSignatureConversion>(&getContext(), c);

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

void populateSymbolToStdPatterns(OwningRewritePatternList& patterns, TypeConverter& c,
                                 MLIRContext* context) {
  c.addConversion([&c](SymbolOrValueType t) -> llvm::Optional<Type> {
    if(t.isSymbolic() == sexprtype::SymbolOrValue::VALUE) {
      return c.convertType(t.getBaseType());
    }
    return llvm::Optional<Type>{};
  });
  c.addConversion([&c](FunctionType t) -> llvm::Optional<Type> {
    auto resultTypes = t.getResults();
    llvm::SmallVector<Type, 1> convertedOutputs{};
    c.convertTypes(resultTypes, convertedOutputs);

    auto inputTypes = t.getInputs();
    llvm::SmallVector<Type, 1> convertedInputs{};
    c.convertTypes(inputTypes, convertedInputs);

    return FunctionType::get(convertedInputs, convertedOutputs, t.getContext());
  });
  c.addConversion([](StringType t) -> llvm::Optional<Type> {
    // Return memory buffer of length + 1 to account for null byte
    return MemRefType::get({static_cast<int64_t>(t.getLength() + 1)},
                           IntegerType::get(8, t.getContext()));
  });

  patterns.insert<SymbolOpLowering, EndOpLowering, ConstantOpLowering, ConstantStringOpLowering>(
      context, c);
}
