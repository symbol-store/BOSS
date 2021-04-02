#include "Engines/MLIREngine.hpp"
#include "Engines/MLIREngine/Analysis/AnalysisPasses.hpp"
#include "Engines/MLIREngine/Dialect/SExprDialect/SExprTypes.h"
#include "Engines/MLIREngine/IR/MLIRGenerator.hpp"
#include "Engines/MLIREngine/Runtime/Runtime.hpp"
#include "Engines/MLIREngine/Translation/SexprToFunctions.hpp"
#include "Engines/MLIREngine/Translation/SexprToLLVM.hpp"
#include "Engines/MLIREngine/Translation/SexprToStd.hpp"
#include "Engines/MLIREngine/Translation/SexprToDatabase.h"

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

  auto status = engine->invoke("entry", args);
  if(status) {
    llvm::errs() << "JIT invocation failed\n";
    throw std::runtime_error("Could not invoke JIT");
  }

  return output;
}

boss::Expression mExpressionFromSExpression(SymbolExpression* expr);

// Retrieve boss::Expression from Symbol/Argument based on given type
const std::map<SymbolArgumentType, std::function<boss::Expression(SymbolArgumentValue)>>
    typeToExpression{
        {SymbolArgumentType::Int, [](SymbolArgumentValue value) { return value.integerValue; }},
        {SymbolArgumentType::Bool, [](SymbolArgumentValue value) { return value.booleanValue; }},
        {SymbolArgumentType::Float, [](SymbolArgumentValue value) { return value.floatValue; }},
        {SymbolArgumentType::Symbol,
         [](SymbolArgumentValue value) { return mExpressionFromSExpression(value.symbolValue); }},
        {SymbolArgumentType::String,
         [](SymbolArgumentValue value) { return std::string(value.stringValue); }}};

boss::Expression mExpressionFromSExpression(SymbolExpression* expr) {
  if(expr->arguments == nullptr) {
    return boss::Symbol{expr->head};
  }

  auto argc = expr->argc;

  boss::ExpressionArguments args;

  for(int i = 0; i < argc; i++) {
    auto const& argument = expr->arguments[i];

    args.push_back(typeToExpression.at(argument.type)(argument.value));
  }

  return boss::ComplexExpression{boss::Symbol{expr->head}, args};
}

Expression Engine::evaluate(Expression const& e) {

  MLIRGenerator generator{};

  auto module = generator.generateModule(e);

  sexprtype::ReturnTypes returnType = sexprtype::ReturnTypes::UNKNOWN;

  ::mlir::PassManager passManager(module->getContext());

  passManager.addPass(createTypeInferencePass(database));
  passManager.addPass(createLowerToFunctionsPass(returnType));
//  passManager.addNestedPass<::mlir::FuncOp>(createLowerToDatabasePass());
  passManager.addNestedPass<::mlir::FuncOp>(createLowerToStdPass());
//  passManager.addPass(::mlir::createInlinerPass());
//  passManager.addPass(createLowerToLLVMPass());

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
  case sexprtype::ReturnTypes::INT:
    return static_cast<int>(jitResult);
  case sexprtype::ReturnTypes::STRING:
    return std::string{reinterpret_cast<const char*>(jitResult)};
  case sexprtype::ReturnTypes::BOOLEAN:
    return static_cast<bool>(jitResult);
  case sexprtype::ReturnTypes::SYMBOL:
    return mExpressionFromSExpression(reinterpret_cast<SymbolExpression*>(jitResult));
  default:
    throw std::runtime_error("Return Type is Unknown");
  }
}

} // namespace boss::engines::mlir
