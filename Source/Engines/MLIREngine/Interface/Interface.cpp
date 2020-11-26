#include "Engines/MLIREngine/Interface/Interface.hpp"

#include "Engines/MLIREngine/AST/Expression.hpp"
#include "Engines/MLIREngine/Analysis/AnalysisPasses.hpp"
#include "Engines/MLIREngine/IR/MLIRVisitor.hpp"
#include "Engines/MLIREngine/Print/PrintVisitor.hpp"
#include "Engines/MLIREngine/Translation/SexprToFunctions.hpp"
#include "Engines/MLIREngine/Translation/SexprToLLVM.hpp"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"
#include <llvm/IR/Module.h>
#include <mlir/ExecutionEngine/ExecutionEngine.h>
#include <mlir/ExecutionEngine/OptUtils.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Target/LLVMIR.h>
#include <mlir/Transforms/Passes.h>

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <utility>

#include <iostream>

int64_t runJit(mlir::ModuleOp module) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  auto maybeEngine = mlir::ExecutionEngine::create(module);

  if(!maybeEngine) {
    return -1;
  }

  auto& engine = maybeEngine.get();

  int64_t output[20]; // NOLINT

  void* args[1] = {(void*)&output}; // NOLINT

  auto status = engine->invoke("entry", args);
  if(status) {
    llvm::errs() << "JIT invocation failed\n";
    return -1;
  }

  return output[0];
}

int64_t Interface::internalEvaluate(mlirengine::Expression& e) {
  PrintVisitor pv{};
  MLIRVisitor mv{};

  e.accept(pv);

  std::cout << std::endl;

  e.accept(mv);
  auto module = mv.getModule();

  if(auto m = module.get()) {
    m.dump();
  }

  std::cout << std::endl;

  mlir::PassManager pm(module->getContext());
  pm.addPass(createTypeInferencePass());

  if(mlir::failed(pm.run(*module))) {
    return -1;
  }

  if(auto m = module.get()) {
    std::cout << "dump typeinference" << std::endl;
    m.dump();
  }

  std::cout << std::endl;

  auto lowerToFuncs = createLowerToFunctionsPass();
  mlir::PassManager lowerManager(module->getContext());

  auto& funcPassManager = lowerManager.nest<mlir::FuncOp>();

  lowerManager.addPass(std::move(lowerToFuncs));

  if(mlir::failed(lowerManager.run(*module))) {
    return -1;
  }

  if(auto m = module.get()) {
    std::cout << "dump lower to functions" << std::endl;
    m.dump();
  }

  std::cout << std::endl;

  funcPassManager.addPass(createLowerToStdPass());

  if(mlir::failed(lowerManager.run(*module))) {
    return -1;
  }

  if(auto m = module.get()) {
    std::cout << "dump lower to std" << std::endl;
    m.dump();
  }

  std::cout << std::endl;

  lowerManager.addPass(mlir::createInlinerPass());

  if(mlir::failed(lowerManager.run(*module))) {
    return -1;
  }

  if(auto m = module.get()) {
    std::cout << "dump inliner" << std::endl;
    m.dump();
  }

  std::cout << std::endl;

  auto llvmPass = createLowerToLLVMPass();
  mlir::PassManager llvmManager(module->getContext());
  llvmManager.addPass(std::move(llvmPass));

  if(mlir::failed(llvmManager.run(*module))) {
    return -1;
  }

  if(auto m = module.get()) {
    m.dump();
  }

  std::cout << std::endl;

  llvm::LLVMContext llvmContext;
  auto llvmModule = mlir::translateModuleToLLVMIR(module.get(), llvmContext);
  if(!llvmModule) {
    llvm::errs() << "Failed to emit LLVM IR\n";
    return -1;
  }

  return runJit(module.get());
}

template <>
std::unique_ptr<mlirengine::StringLiteralExpression>
Interface::evaluate(mlirengine::Expression& e) {
  auto result = internalEvaluate(e);

  auto resAsPtr = reinterpret_cast<const char*>(result);

  std::string resultString{resAsPtr};

  free((void*)resAsPtr);

  return std::make_unique<mlirengine::StringLiteralExpression>(resultString);
}

template <>
std::unique_ptr<mlirengine::IntegerLiteralExpression>
Interface::evaluate(mlirengine::Expression& e) {
  auto result = internalEvaluate(e);

  return std::make_unique<mlirengine::IntegerLiteralExpression>(result);
}
