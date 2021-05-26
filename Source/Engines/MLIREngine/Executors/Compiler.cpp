#include "Compiler.hpp"
#include "Engines/MLIREngine.hpp"
#include "Engines/MLIREngine/Analysis/AnalysisPasses.hpp"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/IR/MLIRGenerator.hpp"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Types/Types.hpp"
#include "Engines/MLIREngine/Types/ValueConversion.hpp"
#include "Engines/MLIREngine/Translation/SexprToFunctions.hpp"
#include "Engines/MLIREngine/Translation/SexprToLLVM.hpp"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"
#include "Engines/MLIREngine/Translation/LowerDatabase.hpp"
#include "Engines/MLIREngine/Types/TypeInference.hpp"

#include <llvm/IR/Module.h>
#include <mlir/ExecutionEngine/ExecutionEngine.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Target/LLVMIR.h>
#include <mlir/Transforms/Passes.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

namespace boss::engines::mlir::compiler {

using namespace boss::mlir::types;

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

  auto status = engine->invoke("entry", args);
  if(status) {
    llvm::errs() << "JIT invocation failed\n";
    throw std::runtime_error("Could not invoke JIT");
  }

  return output;
}

Expression Compiler::evaluate(Expression const& e) {

  MLIRGenerator generator{};

  auto module = generator.generateModule(e);

  // Init return type to error. This gets updated by the lowerToFunctions pass.
  RuntimeTypes returnType = RuntimeTypes::ERROR;

  ::mlir::PassManager passManager(module->getContext());

  boss::mlir::inference::TypeInferenceContext context(module->getContext(), database, {}, nullptr);

  passManager.addPass(createTypeInferencePass(&context));
  passManager.addPass(createLowerToFunctionsPass(returnType));
  passManager.addNestedPass<::mlir::FuncOp>(createLowerToStdPass());
  passManager.addPass(::mlir::createCanonicalizerPass());
  passManager.addPass(::mlir::createInlinerPass());
  passManager.addPass(createLowerToDatabasePass(*database));
  passManager.addPass(::mlir::createCanonicalizerPass());
  passManager.addPass(createLowerToLLVMPass(*database));

  if(::mlir::failed(passManager.run(module.get()))) {
    throw std::runtime_error("Compilation failed");
  }

  module->dump();

  llvm::LLVMContext llvmContext;
  auto llvmModule = ::mlir::translateModuleToLLVMIR(module.get(), llvmContext);
  if(!llvmModule) {
    throw std::runtime_error("Compilation failed");
  }

  auto jitResult = runJit(module.get());

  switch(returnType) {
  case RuntimeTypes::INT:
    return static_cast<int>(jitResult);
  case RuntimeTypes::INT64:
    return static_cast<size_t>(jitResult);
  case RuntimeTypes::STRING:
    return std::string{reinterpret_cast<const char*>(jitResult)};
  case RuntimeTypes::BOOLEAN:
    return static_cast<bool>(jitResult);
  case RuntimeTypes::SYMBOL:
    return boss::mlir::conversion::mExpressionFromSExpression(reinterpret_cast<SymbolExpression*>(jitResult));
  case RuntimeTypes::RELATION:
    return static_cast<size_t>(jitResult);
  default:
    throw std::runtime_error("Return Type is Unknown");
  }
}


}
