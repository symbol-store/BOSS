#pragma once
#include <memory>
#include <mlir/Pass/Pass.h>
#include "Engines/MLIREngine/Types/Types.hpp"

std::unique_ptr<mlir::Pass> createLowerToFunctionsPass(boss::mlir::types::RuntimeTypes& returnType);
