#include "Bulk.hpp"

#include "Bulk/ArrowExtensions/CompoundArray.hpp"
#include "Bulk/ArrowExtensions/ValueArray.hpp"
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
#include "Bulk/BulkExpression.hpp"
#include "Bulk/Executor.hpp"
#include "Bulk/ExpressionVisitDispatcher.hpp"
#include "Bulk/OperatorRegistry.hpp"
#include "Bulk/OperatorUtils.hpp"
#include "Bulk/SymbolRegistry.hpp"

#include <optional>
#include <string>
#include <vector>

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

template <bool canCreateScalar>
BulkExpression createExpression(arrow::ArrayVector&& arrays,
                                std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder,
                                CompoundArray const* parent, size_t childIndex) {
  auto type = arrayBuilder ? arrayBuilder->type() : arrays[0]->type();
  // assuming all arrays and builder share the same type!
  // if not, keep only the latest type
  auto arrayIt = arrays.rbegin();
  for(; arrayIt != arrays.rend(); ++arrayIt) {
    if((*arrayIt)->type_id() != type->id()) {
      break;
    }
  }
  if(arrayIt != arrays.rend()) {
    // shrinked without the arrays having a different type
    arrow::ArrayVector shrinkedArrays(arrayIt.base(), arrays.end());
    arrays.swap(shrinkedArrays);
  }

  switch(type->id()) {
  case arrow::Type::BOOL: {
    auto arrayPtr = std::make_shared<ValueArray<bool>>(std::move(arrays), std::move(arrayBuilder));
    if(canCreateScalar && arrayPtr->length() == 1) {
      auto const& constArray = *arrayPtr;
      return static_cast<bool>(*constArray.begin());
    }
    arrayPtr->setOwner(parent, childIndex);
    return arrayPtr;
  }
  case arrow::Type::INT32: {
    auto arrayPtr = std::make_shared<ValueArray<int>>(std::move(arrays), std::move(arrayBuilder));
    if(canCreateScalar && arrayPtr->length() == 1) {
      auto const& constArray = *arrayPtr;
      return static_cast<int>(*constArray.begin());
    }
    arrayPtr->setOwner(parent, childIndex);
    return arrayPtr;
  }
  case arrow::Type::FLOAT: {
    auto arrayPtr = std::make_shared<ValueArray<float>>(std::move(arrays), std::move(arrayBuilder));
    if(canCreateScalar && arrayPtr->length() == 1) {
      auto const& constArray = *arrayPtr;
      return static_cast<float>(*constArray.begin());
    }
    arrayPtr->setOwner(parent, childIndex);
    return arrayPtr;
  }
  case arrow::Type::STRING: {
    auto arrayPtr =
        std::make_shared<ValueArray<std::string>>(std::move(arrays), std::move(arrayBuilder));
    if(canCreateScalar && arrayPtr->length() == 1) {
      auto const& constArray = *arrayPtr;
      return static_cast<std::string>(*constArray.begin());
    }
    arrayPtr->setOwner(parent, childIndex);
    return arrayPtr;
  }
  case arrow::Type::EXTENSION: {
    auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
    if(extensionType.extension_name()[0] == 's') {
      // SYMBOL
      auto arrayPtr =
          std::make_shared<ValueArray<Symbol>>(std::move(arrays), std::move(arrayBuilder));
      if(canCreateScalar && arrayPtr->length() == 1) {
        auto const& constArray = *arrayPtr;
        return static_cast<Symbol>(*constArray.begin());
      }
      arrayPtr->setOwner(parent, childIndex);
      return arrayPtr;
    }
    // COMPLEX EXPRESSION
    auto arrayPtr =
        std::make_shared<CompoundArray>(std::move(arrays), std::move(arrayBuilder), false);
    if(canCreateScalar && arrayPtr->numRows() == 1) {
      BulkExpressionArguments args;
      auto numColumns = arrayPtr->numArguments();
      args.reserve(numColumns);
      for(int i = 0; i < numColumns; ++i) {
        args.emplace_back(arrayPtr->column(i, true));
      }
      return BulkComplexExpression(arrayPtr->getHead(), args);
    }
    arrayPtr->setOwner(parent, childIndex);
    return arrayPtr;
  }
  default:
    break;
  }

  // [https://github.com/symbol-store/BOSS/issues/97] throw an exception
  return 0; // should not happen!
}

BulkExpression Engine::createArray(arrow::ArrayVector&& arrays,
                                   std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder,
                                   CompoundArray const* parent, size_t childIndex) {
  return createExpression<false>(std::move(arrays), std::move(arrayBuilder), parent, childIndex);
}

BulkExpression Engine::createArrayOrScalar(arrow::ArrayVector&& arrays,
                                           std::shared_ptr<arrow::ArrayBuilder>&& arrayBuilder,
                                           CompoundArray const* parent, size_t childIndex) {
  return createExpression<true>(std::move(arrays), std::move(arrayBuilder), parent, childIndex);
}

/// transform it back to a standard expression
/// changing arrow arrays into lists
Expression toBossExpression(BulkExpression const& bulkExpression) {
  std::optional<Symbol> storedSymbol;
  ExpressionVisitDispatcher<std::shared_ptr<CompoundArray>>::visit(
      [&storedSymbol](auto& tableArrayPtr) {
        auto& tableArray = *tableArrayPtr;
        if(!tableArray.isDecomposed()) {
          return;
        }
        // save the query result into a temporary symbol
        // this is a workaround to avoid a whole table to be converted back
        // to a long list of tuples
        // [https://github.com/symbol-store/BOSS/issues/91] find a way to garbage-collect them
        static int i = 0;
        std::string symbolName = "_table" + std::to_string(i++);
        auto numRows = tableArray.numRows();
        auto numCols = tableArray.numArguments();
        symbolName += "_cols" + std::to_string(numCols) + "rows" + std::to_string(numRows);
        storedSymbol = Symbol(symbolName);
        DefaultSymbolRegistry::instance().registerSymbol(*storedSymbol, tableArrayPtr);
      },
      bulkExpression);
  if(storedSymbol) {
    return *storedSymbol;
  }

  std::optional<Symbol> rootHead;
  ExpressionArguments arguments;
  std::visit(utilities::overload(
                 [&arguments, &rootHead](std::shared_ptr<CompoundArray> const& compoundArrayPtr) {
                   auto const& compoundArray = *compoundArrayPtr;
                   arguments.reserve(compoundArray.length());
                   rootHead = compoundArray.getHead();
                   for(size_t index = 0; index < compoundArray.length(); ++index) {
                     auto extractedExpr = compoundArray.extract(index);
                     arguments.emplace_back(toBossExpression(extractedExpr));
                   }
                 },
                 [&arguments, &rootHead](BulkComplexExpression const& complexExpression) {
                   auto const& bulkArguments = complexExpression.getArguments();
                   arguments.reserve(bulkArguments.size());
                   rootHead = complexExpression.getHead();
                   for(auto const& bulkArgument : bulkArguments) {
                     arguments.emplace_back(toBossExpression(bulkArgument));
                   }
                 },
                 [&arguments](auto const& other) {
                   if constexpr(std::is_constructible_v<Expression, decltype(other)>) {
                     arguments.emplace_back(other);
                   } else {
                     auto const& valueArray = *other;
                     arguments.reserve(valueArray.length());
                     using ValueArrayType = std::decay_t<decltype(valueArray)>;
                     for(auto const& value : valueArray) {
                       arguments.emplace_back(
                           static_cast<typename ValueArrayType::ValueType>(value));
                     }
                   }
                 }),
             (BulkExpression::SuperType const&)bulkExpression);

  if(arguments.size() == 1 && !rootHead) {
    return arguments[0];
  }

  Symbol const& head = rootHead ? *rootHead : Symbol("List");
  return ComplexExpression(head, arguments);
}

Expression Engine::evaluate(Expression const& e) { // NOLINT
  auto output = Executor::evaluate(e);

  // transform it back to a standard expression
  // changing arrow arrays into lists
  return toBossExpression(output);
}

} // namespace boss::engines::bulk
