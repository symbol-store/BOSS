#include "Engines/MLIREngine.hpp"
#include "Engines/MLIREngine/Analysis/AnalysisPasses.hpp"
#include "Engines/MLIREngine/Dialect/SExprTypes.h"
#include "Engines/MLIREngine/IR/MLIRGenerator.hpp"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Translation/SexprToFunctions.hpp"
#include "Engines/MLIREngine/Translation/SexprToLLVM.hpp"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"

#include <llvm/IR/Module.h>
#include <mlir/ExecutionEngine/ExecutionEngine.h>
#include <mlir/ExecutionEngine/OptUtils.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Target/LLVMIR.h>
#include <mlir/Transforms/Passes.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ErrorOr.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <stdio.h>

#include <stdexcept>

namespace boss::engines::mlir {

// Run the module in the LLVM JIT
int64_t runJit(::mlir::ModuleOp module) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  auto maybeEngine = ::mlir::ExecutionEngine::create(module);

  if(!maybeEngine) {
    throw std::runtime_error("Could not create JIT");
  }

  auto& engine = maybeEngine.get();

  int64_t output; // NOLINT

  void* args[1] = {(void*)&output}; // NOLINT

  // engine->registerSymbols(llvm::function_ref(sayHello));

  auto status = engine->invoke("entry", args);
  if(status) {
    llvm::errs() << "JIT invocation failed\n";
    throw std::runtime_error("Could not invoke JIT");
  }

  return output;
}

Expression Engine::evaluate(Expression const& e) {

  MLIRGenerator generator{};

  auto module = generator.generateModule(e);

  sexprtype::ReturnTypes returnType = sexprtype::ReturnTypes::UNKNOWN;

  ::mlir::PassManager passManager(module->getContext());
  auto& funcPassManager = passManager.nest<::mlir::FuncOp>();

  passManager.addPass(createTypeInferencePass());
  passManager.addPass(createLowerToFunctionsPass(returnType));
  passManager.addNestedPass<::mlir::FuncOp>(createLowerToStdPass());
  passManager.addPass(::mlir::createInlinerPass());
  passManager.addPass(createLowerToLLVMPass());

  module->dump();

  if(::mlir::failed(passManager.run(module.get()))) {
    throw std::runtime_error("Compilation failed");
  }

  llvm::LLVMContext llvmContext;
  auto llvmModule = ::mlir::translateModuleToLLVMIR(module.get(), llvmContext);
  if(!llvmModule) {
    llvmModule->dump();
    throw std::runtime_error("Compilation failed");
  }

  // module->dump();

  auto jitResult = runJit(module.get());

  switch(returnType) {
  case sexprtype::ReturnTypes::INT:
    return static_cast<int>(jitResult);
  case sexprtype::ReturnTypes::STRING:
    return std::string{reinterpret_cast<const char*>(jitResult)};
  case sexprtype::ReturnTypes::BOOLEAN:
    return static_cast<bool>(jitResult);
  case sexprtype::ReturnTypes::SYMBOL:
    return mExpressionFromSExpression(reinterpret_cast<SExpression*>(jitResult));
  default:
    throw std::runtime_error("Return Type is Unknown");
  }
}

} // namespace boss::engines::mlir
