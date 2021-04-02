#pragma once
#include <memory>
#include <mlir/Pass/Pass.h>

namespace runtime {
class Database;
}

std::unique_ptr<mlir::Pass> createTypeInferencePass(runtime::Database const& database);
