#pragma once
#include <memory>
#include <mlir/Pass/Pass.h>

std::unique_ptr<mlir::Pass> createLowerToFunctionsPass();
std::unique_ptr<mlir::Pass> createLowerReturnPass();
