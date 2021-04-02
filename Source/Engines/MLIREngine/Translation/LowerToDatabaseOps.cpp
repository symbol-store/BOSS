#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseDialect.h"
#include "Engines/MLIREngine/Dialect/DatabaseDialect/DatabaseOps.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprDialect.h"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprOps.h"
#include "SexprToDatabase.h"
#include <mlir/Dialect/SCF/SCF.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/IR/StandardTypes.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>

#include <unordered_set>

namespace {
using namespace mlir;

struct SexprToDatabaseLoweringPass : public PassWrapper<SexprToDatabaseLoweringPass, FunctionPass> {

  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<mlir::database::DatabaseDialect>();
  }

  void runOnFunction() final;
};

const std::map<std::string,
               std::function<LogicalResult(sexpr::SymbolOp s, PatternRewriter& rewriter)>>
    databaseOpsDispatchTable{
        {"GetRelation", [](sexpr::SymbolOp s, PatternRewriter& rewriter) {
           auto stringVal = s.getOperand(0);
           auto stringConstantOp = stringVal.getDefiningOp<sexpr::StringConstantOp>();

           if (!stringConstantOp) {
             return failure();
           }

           auto relationName = stringConstantOp.value().str();
           auto newOp = rewriter.create<database::GetRelationOp>(s.getLoc(), relationName, s.getResult().getType());

           s.dump();
           newOp.dump();

           rewriter.replaceOp(s, newOp.getResult());

           return success();
         }}};

struct SymbolOpLowering : public OpRewritePattern<sexpr::SymbolOp> {
  SymbolOpLowering(MLIRContext* ctx) : OpRewritePattern<sexpr::SymbolOp>(ctx) {}

  LogicalResult matchAndRewrite(sexpr::SymbolOp s, PatternRewriter& rewriter) const override {

    auto symbolName = s.name();

    // Dispatch table to insert appropriate
    auto it = databaseOpsDispatchTable.find(std::string{symbolName});
    if(it != databaseOpsDispatchTable.end()) {
      // Function is defined, replace operation
      return it->second(s, rewriter);
    }

    return failure();
  }
};

void SexprToDatabaseLoweringPass::runOnFunction() {
  ConversionTarget target(getContext());

  // Mark all Symbols containing database operations as illegal
  target.addDynamicallyLegalDialect<sexpr::SExprDialect>([](Operation* o) -> bool {
    //    if (auto op = mlir::dyn_cast_or_null<sexpr::SymbolOp>(o)) {
    //      return databaseOpsDispatchTable.find(op.name().str()) == databaseOpsDispatchTable.end();
    //    }
    //    return true;
    return false;
  });

  target.addLegalDialect<database::DatabaseDialect>();

  OwningRewritePatternList patterns;
  patterns.insert<SymbolOpLowering>(&getContext());
  auto res = applyPartialConversion(getFunction(), target, std::move(patterns));

  if(failed(res)) {
    signalPassFailure();
  }
}

} // namespace

std::unique_ptr<mlir::Pass> createLowerToDatabasePass() {
  return std::make_unique<SexprToDatabaseLoweringPass>();
}