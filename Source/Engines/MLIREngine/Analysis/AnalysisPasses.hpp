#pragma once
#include <memory>
#include <mlir/Pass/Pass.h>

namespace boss::mlir::inference {
class TypeInferenceContext;
}

std::unique_ptr<mlir::Pass> createTypeInferencePass(boss::mlir::inference::TypeInferenceContext*);
