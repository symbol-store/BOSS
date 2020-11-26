#pragma once
#include "Engines/MLIREngine/Dialect/SExprTypes.h"
#include <memory>
#include <mlir/Pass/Pass.h>

std::unique_ptr<mlir::Pass> createLowerToFunctionsPass(sexprtype::ReturnTypes& returnType);
