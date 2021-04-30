#pragma once
#include <memory>
#include <mlir/Pass/Pass.h>

namespace new_runtime {
class Database;
}

std::unique_ptr<mlir::Pass> createTypeInferencePass(new_runtime::Database const& database);
