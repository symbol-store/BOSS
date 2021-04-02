#include "AnalysisPasses.hpp"
#include "Engines/MLIREngine/Dialect/TypeInferenceInterface.h"
#include <iostream>

using namespace mlir;

namespace {
struct TypeInferencePass : PassWrapper<TypeInferencePass, OperationPass<ModuleOp>> {
  explicit TypeInferencePass(const runtime::Database& database) : database(database) {}
  void runOnOperation() final;

private:
  runtime::Database const& database;
};
} // namespace

void TypeInferencePass::runOnOperation() {
  auto module = mlir::cast<ModuleOp, Operation>(getOperation());

  auto& ops = module.getBody()->getOperations();

  dyn_cast<TypeInference, Operation>(ops.front()).inferType(database);
}

std::unique_ptr<mlir::Pass> createTypeInferencePass(runtime::Database const& database) {
  return std::make_unique<TypeInferencePass>(database);
}
