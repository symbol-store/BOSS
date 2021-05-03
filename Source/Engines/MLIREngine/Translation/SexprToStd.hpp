#pragma once
#include <memory>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>

std::unique_ptr<mlir::Pass> createLowerToStdPass();

void populateSymbolToStdPatterns(::mlir::OwningRewritePatternList& patterns,
                                 ::mlir::TypeConverter& c, ::mlir::MLIRContext* context);
