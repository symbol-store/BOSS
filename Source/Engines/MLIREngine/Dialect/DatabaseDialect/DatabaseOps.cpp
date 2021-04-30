#include "DatabaseOps.h"
#include "DatabaseTypes.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include <mlir/Transforms/DialectConversion.h>

#define GET_OP_CLASSES
#include "DatabaseOps.cpp.inc"

TupleStreamUnionType mlir::database::GetRelationOp::getTupleStream() {
  return getType().cast<TupleStreamUnionType>();
}

struct RemoveTupleStreamPack
    : public ::mlir::OpRewritePattern<mlir::database::ExtractFieldFromTupleOp> {
  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(mlir::database::ExtractFieldFromTupleOp op,
                                      mlir::PatternRewriter& rewriter) const override {
    auto packingOp = mlir::dyn_cast_or_null<mlir::database::PackFieldsIntoTupleOp>(
        op.getOperand().getDefiningOp());
    if(!packingOp) {
      return mlir::failure();
    }

    auto targetFieldName = op.fieldName().str();

    // Get the index that the field corresponds to in the pack
    auto tupleStream = packingOp.getType().cast<TupleStreamType>().getFields();
    auto tupleIterator =
        std::find_if(tupleStream.begin(), tupleStream.end(),
                     [&targetFieldName](auto el) { return el.first == targetFieldName; });
    if(tupleIterator == tupleStream.end()) {
      op.emitError("The field " + targetFieldName + " does not exist.");
      return mlir::failure();
    }
    auto tupleIndex = std::distance(tupleStream.begin(), tupleIterator);

    // Get the value at the index of the pack
    auto value = packingOp.getOperand(tupleIndex);
    op.replaceAllUsesWith(value);
    op.erase();

    return mlir::success();
  }
};

struct RemoveTupleStreamUnionPack
    : public ::mlir::OpRewritePattern<mlir::database::GetTupleStreamFromUnion> {
  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(mlir::database::GetTupleStreamFromUnion op,
                                      mlir::PatternRewriter& rewriter) const override {
    auto packingOp = mlir::dyn_cast_or_null<mlir::database::CreateUnionTupleStream>(
        op.getOperand().getDefiningOp());
    if (!packingOp) {
      return mlir::failure();
    }

    auto tupleStream = packingOp.getType().dyn_cast_or_null<TupleStreamUnionType>();
    if (!tupleStream) {
      return mlir::failure();
    }

    if (op.fieldIndex() >= packingOp.getOperation()->getNumOperands()) {
      op.emitError("The operation does not have " + std::to_string(op.fieldIndex()) + " operands");
      return mlir::failure();
    }

    auto element = packingOp.getOperation()->getOperand(op.fieldIndex());
    op.replaceAllUsesWith(element);
    op.erase();

    return mlir::success();
  }
};


void mlir::database::ExtractFieldFromTupleOp::getCanonicalizationPatterns(
    ::mlir::OwningRewritePatternList& results, ::mlir::MLIRContext* context) {
  results.insert<RemoveTupleStreamPack>(context);
}

void mlir::database::GetTupleStreamFromUnion::getCanonicalizationPatterns(
    ::mlir::OwningRewritePatternList& results, ::mlir::MLIRContext* context) {
  results.insert<RemoveTupleStreamUnionPack>(context);
}