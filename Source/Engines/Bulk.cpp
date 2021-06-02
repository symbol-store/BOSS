#include "Bulk.hpp"

#include "Bulk/Batch/CompoundBatch.hpp"
#include "Bulk/BuiltinFunctions/Aggregates.hpp"
#include "Bulk/BuiltinFunctions/ArithmeticFunctions.hpp"
#include "Bulk/BuiltinFunctions/Collections.hpp"
#include "Bulk/BuiltinFunctions/ComparisonFunctions.hpp"
#include "Bulk/BuiltinFunctions/ConversionFunctions.hpp"
#include "Bulk/BuiltinFunctions/DBManagementOps.hpp"
#include "Bulk/BuiltinFunctions/LogicFunctions.hpp"
#include "Bulk/BuiltinFunctions/Queries.hpp"
#include "Bulk/BuiltinFunctions/StringFunctions.hpp"
#include "Bulk/BuiltinFunctions/SymbolicFunctions.hpp"
#include "Bulk/Executor.hpp"
#include "Bulk/OperatorRegistry.hpp"
#include "Bulk/OperatorUtils.hpp"
#include "Bulk/SymbolRegistry.hpp"

#include <optional>

namespace boss::engines::bulk {

using OperatorUtilsImpl = OperatorUtils<bool, int, float, std::string>;

using OperatorRegistryWithExecutor = OperatorRegistry<Executor>;

Engine::Engine() {
  DefaultSymbolRegistry::instance().clear();

  static bool registered = false;
  if(!registered) {
    // register all built-in functions here
    ArithmeticFunctions<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    ComparisonFunctions<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    LogicFunctions<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    ConversionFunctions<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    StringFunctions<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    SymbolicFunctions<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    Collections<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    Aggregates<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    DBManagementOps<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
    Queries<OperatorUtilsImpl, OperatorRegistryWithExecutor>::registerAll();
  }
}

Batch* createBatch(Expression const& expression) {
  return std::visit([](auto&& value) { return createBatch(value); },
                    (Expression::SuperType const&)expression);
}

Batch* createBatch(ComplexExpression const& expression) {
  auto* newBatch = new CompoundBatch(expression.getHead());
  newBatch->append(expression);
  return newBatch;
}

template <typename T> Batch* createBatch(T const& value) {
  return new ValueBatch<T>(1, value);
}

/// convert the batch back to an expression
Expression revertToExpression(Batch::ReadablePtr&& batchPtr) {
  bool handledAsSymbol = false;
  std::string symbolName;
  boss::engines::bulk::BatchVisitDispatcher<CompoundBatch>::visit(
      [&handledAsSymbol, &symbolName, &batchPtr](auto& tableBatch) {
        if(!tableBatch.isDecomposed()) {
          return;
        }
        // save the query result into a temporary symbol
        // this is a workaround to avoid a whole table to be converted back
        // to a long list of tuples
        // [https://github.com/symbol-store/BOSS/issues/91] find a way to garbage-collect them
        static int i = 0;
        symbolName = "_table" + std::to_string(i++);
        auto numRows = tableBatch.size();
        auto numCols = tableBatch.numColumns();
        symbolName += "_cols" + std::to_string(numCols) + "rows" + std::to_string(numRows);
        Symbol savedSymbol(symbolName);
        auto& savedSymbolPtr = DefaultSymbolRegistry::instance().findSymbol(savedSymbol);
        savedSymbolPtr = std::move(batchPtr);
        handledAsSymbol = true;
      },
      *batchPtr);
  if(handledAsSymbol) {
    return Symbol(symbolName);
  }

  auto const& batch = *batchPtr;
  std::optional<Symbol> rootHead;
  ExpressionArguments arguments;
  arguments.reserve(batch.size());
  OperatorUtilsImpl::AnyBatchVisitDispatcher::visit(
      [&arguments, &rootHead, batchPtr{std::move(batchPtr)}](auto const& batch) {
        using BatchType = std::decay_t<decltype(batch)>;
        if constexpr(std::is_base_of_v<CompoundBatch, BatchType>) {
          rootHead = batch.getHead();
          size_t batchSize = batch.size();
          for(size_t index = 0; index < batchSize; ++index) {
            auto extractedPtr = batch.extract(index);
            arguments.emplace_back(revertToExpression(std::move(extractedPtr)));
          }
        } else {
          for(auto const& value : batch) {
            arguments.emplace_back(static_cast<typename BatchType::ValueType>(value));
          }
        }
      },
      batch);

  if(arguments.size() == 1 && !rootHead) {
    return arguments[0];
  }

  Symbol const& head = rootHead ? *rootHead : Symbol("List");
  return ComplexExpression(head, arguments);
}

Expression Engine::evaluate(Expression const& e) { // NOLINT
  Batch::WritablePtr batchPtr;
  auto const* expr = std::get_if<ComplexExpression>(&e);
  bool done = false;
  if(expr != nullptr && expr->getHead().getName() == "List" && !expr->getArguments().empty()) {
    // special case if the root head is a list
    // can just put all arguments in a single batch
    auto argsIt = expr->getArguments().begin();
    batchPtr = Batch::WritablePtr(createBatch(*argsIt));
    auto& batch = *batchPtr;

    // still check if all arguments are compatible
    // if not need to fallback to normal method
    done = true;
    for(++argsIt; argsIt != expr->getArguments().end(); ++argsIt) {
      auto const& exprArg = *argsIt;
      if(!batch.canContain(exprArg)) {
        done = false;
        break;
      }
      batch.append(exprArg);
    }
  }

  if(!done) {
    // default case, create just a single element batch for the root expression
    batchPtr = Batch::WritablePtr(createBatch(e));
  }
  
  Batch::ReadablePtr outputPtr;
  if(!Executor::evaluate(*batchPtr, outputPtr)) {
    return e;
  }

  // transform the batch back to an expression
  return revertToExpression(std::move(outputPtr));
}

} // namespace boss::engines::bulk
