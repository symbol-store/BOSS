#include "AnalysisPasses.hpp"
#include "Engines/MLIREngine/Dialect/TypeInferenceInterface.h"
#include <iostream>

using namespace mlir;

namespace {
struct TypeInferencePass : PassWrapper<TypeInferencePass, OperationPass<ModuleOp>> {
  void runOnOperation() final;
};
} // namespace

void TypeInferencePass::runOnOperation() {
  auto module = mlir::cast<ModuleOp, Operation>(getOperation());

  auto& ops = module.getBody()->getOperations();

  dyn_cast<TypeInference, Operation>(ops.front()).inferType();
}

std::unique_ptr<mlir::Pass> createTypeInferencePass() {
  return std::make_unique<TypeInferencePass>();
}
