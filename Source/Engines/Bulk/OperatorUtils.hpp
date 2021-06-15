#pragma once

#include "ArrowExtensions/CompoundArray.hpp"
#include "ArrowExtensions/ValueArray.hpp"
#include "ExpressionVisitDispatcher.hpp"
#include "OperatorUtils.hpp"

namespace boss::engines::bulk {

/** A set of util functions which can be called by the operators to avoid repeating common code.*/
template <typename... SupportedTypes> class OperatorUtils {
private:
  // to be passed as template argument to define compile-time symbol names
  // used for operator's argument resolution
  static char constexpr functionSymbolName[] = "Function"; // NOLINT
  static char constexpr listSymbolName[] = "List";         // NOLINT
  static char constexpr symbolSymbolName[] = "Symbol";     // NOLINT

public:
  // common specific complex expression heads
  using FunctionHeadOnly = CompileTimeSymbol<&functionSymbolName[0]>;
  using ListHeadOnly = CompileTimeSymbol<&listSymbolName[0]>;
  using SymbolHeadOnly = CompileTimeSymbol<&symbolSymbolName[0]>;

  // common argument types

  using AnyTypeArgument =
      AllowedArguments<SupportedTypes..., Symbol, BulkComplexExpression,
                       std::shared_ptr<ValueArray<SupportedTypes>>...,
                       std::shared_ptr<ValueArray<Symbol>>, std::shared_ptr<CompoundArray>>;

  using AnySimpleTypeArgument = AllowedArguments<SupportedTypes..., Symbol>;

  template <typename T>
  using SimpleTypeOrCollection = AllowedArguments<T, std::shared_ptr<ValueArray<T>>>;

  using AnySimpleTypeCollectionArgument =
      AllowedArguments<ListHeadOnly, std::shared_ptr<ValueArray<SupportedTypes>>...,
                       std::shared_ptr<ValueArray<Symbol>>>;

  using AnyCollectionArgument =
      AllowedArguments<ListHeadOnly, std::shared_ptr<ValueArray<SupportedTypes>>...,
                       std::shared_ptr<ValueArray<Symbol>>, std::shared_ptr<CompoundArray>>;

  using AnySymbolicArgument =
      AllowedArguments<Symbol, BulkComplexExpression, std::shared_ptr<ValueArray<Symbol>>,
                       std::shared_ptr<CompoundArray>>;

  using FunctionArgument = AllowedArguments<FunctionHeadOnly>;
  using ListArgument = AllowedArguments<ListHeadOnly>;
  using TableArgument = AllowedArguments<std::shared_ptr<CompoundArray>>;
  using TableOrSymbolArgument = AllowedArguments<Symbol, std::shared_ptr<CompoundArray>>;
  using TableOrListArgument = AllowedArguments<ListHeadOnly, std::shared_ptr<CompoundArray>>;

  // common visit dispatcher types

  using AnyTypeVisitDispatcher =
      ExpressionVisitDispatcher<SupportedTypes..., Symbol, BulkComplexExpression,
                                std::shared_ptr<ValueArray<SupportedTypes>>...,
                                std::shared_ptr<ValueArray<Symbol>>,
                                std::shared_ptr<CompoundArray>>;

  using CollectionVisitDispatcher =
      ExpressionVisitDispatcher<std::shared_ptr<ValueArray<SupportedTypes>>...,
                                std::shared_ptr<ValueArray<Symbol>>,
                                std::shared_ptr<CompoundArray>>;

  using SimpleTypeVisitDispatcher = ExpressionVisitDispatcher<SupportedTypes..., Symbol>;

  /// To iterate on all the input arrays at once
  /// and call a function on each tuple of elements from input arrays.
  /// Return an array with a return type resolved from the function Func to call.
  template <typename Func, typename... ArrayPtrIn>
  static BulkExpression evaluateForEachTuple(Func&& func, ArrayPtrIn&&... in) {
    auto apply = [&](auto& out, auto&&... inIt) {
      auto outIt = out.begin();
      for(; outIt != out.end(); ++outIt, ((++inIt), ...)) {
        *outIt = func((FromArrayTypeToElementType<
                       typename std::remove_reference_t<ArrayPtrIn>::element_type>)(*inIt)...);
      }
    };

    using ReturnType = ReturnType<std::decay_t<decltype(func)>, std::decay_t<decltype(in)>...>;
    using ArrayType = std::conditional_t<std::is_same_v<ReturnType, BulkComplexExpression>,
                                         CompoundArray, ValueArray<ReturnType>>;

    auto output = std::make_shared<ArrayType>();
    size_t outputSize = 1;
    (..., [&outputSize, &in]() { outputSize = std::max(outputSize, in->length()); }());
    output->resize(outputSize);
    apply(*output,
          reinterpret_cast<typename std::remove_reference_t<ArrayPtrIn>::element_type const*>(
              in.get())
              ->begin()...);
    return output;
  }

  static BulkExpression getColumnNames(CompoundArray const& srcArray) {
    // create a temporary Symbol array from the field names
    auto const& childFields = srcArray.childFields();
    auto columnsPtr = std::make_shared<ValueArray<Symbol>>();
    auto& columns = *columnsPtr;
    columns.resize(childFields.size());
    auto columnIt = columns.begin();
    for(auto const& field : childFields) {
      *columnIt = Symbol(field->name());
      ++columnIt;
    }
    return columnsPtr;
  }

  template <typename DestArrayType, typename SrcArrayType>
  static void insertAllRows(DestArrayType& destArray, SrcArrayType const& srcArray) {
    std::vector<ArrayData> argData;
    argData.reserve(srcArray.numArguments());
    for(auto srcArg : srcArray) {
      CollectionVisitDispatcher::visit(
          [&argData](auto const& srcColumnPtr) { argData.emplace_back(srcColumnPtr->data()); },
          Executor::evaluate(srcArg)); // evaluate the column before inserting it
    }
    destArray.append(srcArray.getHead(), argData);
  }

  /// copy row values in sorted order (based on indices), column per column
  template <typename DestArrayType, typename SrcArrayType>
  static void insertRowValuesInOrder(DestArrayType& destArray, SrcArrayType const& srcArray,
                                     std::vector<size_t> const& rowIndices, bool needResize = true,
                                     size_t offset = 0) {
    static_assert(std::is_same_v<std::remove_const_t<SrcArrayType>, DestArrayType>);
    // special case for columns of complex expressions
    if constexpr(std::is_base_of_v<CompoundArray, SrcArrayType>) {
      // evaluate the columns before inserting them
      BulkExpressionArguments evaluatedSrcArgs;
      evaluatedSrcArgs.reserve(srcArray.numArguments());
      for(auto srcArg : srcArray) {
        evaluatedSrcArgs.emplace_back(Executor::evaluate(srcArg));
      }
      // make sure the columns exist in the destination
      // but just initialise them empty so far
      std::vector<ArrayData> argData;
      argData.reserve(evaluatedSrcArgs.size());
      for(auto const& srcArg : evaluatedSrcArgs) {
        CollectionVisitDispatcher::visit(
            [&argData](auto const& srcColumnPtr) { argData.emplace_back(srcColumnPtr->data()); },
            srcArg);
      }
      destArray.initArguments(srcArray.getHead(), argData);
      // then recursive call for every argument
      if(needResize) {
        size_t prevSize = destArray.numRows();
        offset += prevSize;
        size_t numRowsToInsert = rowIndices.size();
        destArray.resize(prevSize + numRowsToInsert);
      }
      auto destArgIt = destArray.begin();
      for(auto const& srcArg : evaluatedSrcArgs) {
        CollectionVisitDispatcher::visit(
            [&destArgIt, &rowIndices, &offset](auto const& srcColumnPtr) {
              using ColumnType = std::decay_t<decltype(srcColumnPtr)>;
              // insert to existing arg column
              // assuming destination column is same type
              auto destArg = *destArgIt;
              ExpressionVisitDispatcher<ColumnType>::visit(
                  [&srcColumnPtr, &rowIndices, &offset](auto& destColumnPtr) {
                    auto& destColumn = *destColumnPtr;
                    insertRowValuesInOrder(destColumn, *srcColumnPtr, rowIndices, false, offset);
                  },
                  destArg);
              ++destArgIt;
            },
            srcArg);
      }
      return;
    }
    // general case (for value arrays)
    if(needResize) {
      auto prevSize = destArray.length();
      offset += prevSize;
      size_t numRowsToInsert = rowIndices.size();
      destArray.resize(prevSize + numRowsToInsert);
    }
    auto destValueIt = destArray.begin() + offset;
    for(auto rowIndexIt = rowIndices.begin(); rowIndexIt != rowIndices.end();
        ++rowIndexIt, ++destValueIt) {
      auto srcValueIt = srcArray.begin() + *rowIndexIt;
      *destValueIt = *srcValueIt;
    }
  }

  /// copy row values if matches a condition, column per column
  template <typename DestArrayType, typename SrcArrayType, typename ConditionArrayType>
  static void insertRowValuesWithCondition(DestArrayType& destArray, SrcArrayType const& srcArray,
                                           ConditionArrayType const& conditionArray,
                                           bool needResize = true, size_t offset = 0) {
    static_assert(std::is_same_v<std::remove_const_t<SrcArrayType>, DestArrayType>);
    // special case for columns of complex expressions
    if constexpr(std::is_base_of_v<CompoundArray, SrcArrayType>) {
      // evaluate the columns before inserting them
      BulkExpressionArguments evaluatedSrcArgs;
      evaluatedSrcArgs.reserve(srcArray.numArguments());
      for(auto srcArg : srcArray) {
        evaluatedSrcArgs.emplace_back(Executor::evaluate(srcArg));
      }
      // make sure the columns exist in the destination
      // but just initialise them empty so far
      std::vector<ArrayData> argData;
      argData.reserve(evaluatedSrcArgs.size());
      for(auto const& srcArg : evaluatedSrcArgs) {
        CollectionVisitDispatcher::visit(
            [&argData](auto const& srcColumnPtr) { argData.emplace_back(srcColumnPtr->data()); },
            srcArg);
      }
      destArray.initArguments(srcArray.getHead(), argData);
      // then recursive call for every argument
      if(needResize) {
        size_t prevSize = destArray.numRows();
        offset += prevSize;
        size_t numRowsToInsert = conditionArray.calculateBitCount();
        destArray.resize(prevSize + numRowsToInsert);
      }
      auto destArgIt = destArray.begin();
      for(auto const& srcArg : evaluatedSrcArgs) {
        CollectionVisitDispatcher::visit(
            [&destArgIt, &conditionArray, &offset](auto const& srcColumnPtr) {
              using ColumnType = std::decay_t<decltype(srcColumnPtr)>;
              // insert to existing arg column
              // assuming destination column is same type
              auto destArg = *destArgIt;
              ExpressionVisitDispatcher<ColumnType>::visit(
                  [&srcColumnPtr, &conditionArray, &offset](auto& destColumnPtr) {
                    auto& destColumn = *destColumnPtr;
                    insertRowValuesWithCondition(destColumn, *srcColumnPtr, conditionArray, false,
                                                 offset);
                  },
                  destArg);
              ++destArgIt;
            },
            srcArg);
      }
      return;
    }
    // general case (for value arrays)
    if(needResize) {
      auto prevSize = destArray.length();
      offset += prevSize;
      size_t numRowsToInsert = conditionArray.calculateBitCount();
      destArray.resize(prevSize + numRowsToInsert);
    }
    auto destValueIt = destArray.begin() + offset;
    auto srcValueIt = srcArray.begin();
    auto conditionIt = conditionArray.begin();
    for(; srcValueIt != srcArray.end(); ++srcValueIt, ++conditionIt) {
      if(*conditionIt) {
        *destValueIt = *srcValueIt;
        ++destValueIt;
      }
    }
  }

private:
  // to retrieve a return type for a certain set of typed arguments when calling Func
  template <typename T> using FromArrayTypeToElementType = typename T::ValueType;
  template <typename Func, typename... ArrayPtrTypes>
  using ReturnType = typename std::invoke_result_t<
      Func, FromArrayTypeToElementType<typename ArrayPtrTypes::element_type>...>;
};

} // namespace boss::engines::bulk
