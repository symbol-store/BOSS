#include "AnalysisPasses.hpp"
#include "Engines/MLIREngine/Dialect/TypeInferenceInterface.h"
#include <iostream>

using namespace mlir;

namespace {
struct TypeInferencePass : PassWrapper<TypeInferencePass, OperationPass<ModuleOp>> {
  explicit TypeInferencePass(boss::mlir::inference::TypeInferenceContext* context) : context(context) {}
  void runOnOperation() final;

private:
  boss::mlir::inference::TypeInferenceContext* context;
};
} // namespace

void TypeInferencePass::runOnOperation() {
  auto module = mlir::cast<ModuleOp, Operation>(getOperation());

  auto& ops = module.getBody()->getOperations();

  dyn_cast<TypeInference, Operation>(ops.front()).inferType(context);
}

std::unique_ptr<mlir::Pass> createTypeInferencePass(boss::mlir::inference::TypeInferenceContext* context) {
  return std::make_unique<TypeInferencePass>(context);
}
