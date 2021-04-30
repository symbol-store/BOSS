#pragma once
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include <memory>
#include <mlir/Pass/Pass.h>

std::unique_ptr<mlir::Pass> createLowerToDatabasePass(new_runtime::Database& database);