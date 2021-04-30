#pragma once
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include <memory>
#include <mlir/Pass/Pass.h>

std::unique_ptr<mlir::Pass> createLowerToLLVMPass(new_runtime::Database& database);
