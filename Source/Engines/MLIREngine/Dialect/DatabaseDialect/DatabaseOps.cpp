#include "DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "DatabaseTypes.h"
#include <mlir/Transforms/DialectConversion.h>

#define GET_OP_CLASSES
#include "DatabaseOps.cpp.inc"

TupleStreamType mlir::database::GetRelationOp::getTupleStream() {
  return getType().cast<TupleStreamType>();
}

struct RemoveRedundantPackAndExtract : public ::mlir::OpRewritePattern<mlir::database::ExtractFieldFromTupleOp> {
  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(mlir::database::ExtractFieldFromTupleOp op,
                                      mlir::PatternRewriter& rewriter) const override {
    auto packingOp = mlir::dyn_cast_or_null<mlir::database::PackFieldsIntoTupleOp>(op.getOperand().getDefiningOp());
    if (!packingOp) {
      return mlir::failure();
    }

    auto targetFieldName = op.fieldName().str();

    // Get the index that the field corresponds to in the pack
    auto tupleStream = packingOp.getType().cast<TupleStreamType>().getConcreteTupleTypes();
    auto tupleIterator = std::find_if(tupleStream.begin(), tupleStream.end(),
                                      [&targetFieldName](auto el){ return el.first == targetFieldName; });
    if (tupleIterator == tupleStream.end()) {
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

void mlir::database::ExtractFieldFromTupleOp::getCanonicalizationPatterns(::mlir::OwningRewritePatternList &results, ::mlir::MLIRContext *context) {
  results.insert<RemoveRedundantPackAndExtract>(context);
}