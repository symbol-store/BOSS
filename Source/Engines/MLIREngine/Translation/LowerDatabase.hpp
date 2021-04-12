#pragma once
#include "Engines/MLIREngine/Runtime/Database.hpp"
#include <memory>
#include <mlir/Pass/Pass.h>

std::unique_ptr<mlir::Pass> createLowerToDatabasePass(runtime::Database& database);