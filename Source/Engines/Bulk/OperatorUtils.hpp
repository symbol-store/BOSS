#pragma once

#include "Batch/Batch.hpp"
#include "BatchVisitDispatcher.hpp"

namespace boss::engines::bulk {

/** A set of util functions which can be called by the operators to avoid repeating common code.*/
template <typename... SupportedTypes> class OperatorUtils {
public:
  // common argument types

  using AnyTypeArgument =
      AllowedArguments<SupportedTypes..., Symbol, BulkComplexExpression,
                       std::shared_ptr<ValueArray<SupportedTypes>>...,
                       std::shared_ptr<ValueArray<Symbol>>, std::shared_ptr<CompoundArray>>;

  using AnySimpleTypeArgument = AllowedArguments<SupportedTypes..., Symbol>;

  using AnySimpleTypeCollectionArgument =
      AllowedArguments<std::shared_ptr<ValueArray<SupportedTypes>>...,
                       std::shared_ptr<ValueArray<Symbol>>, BulkComplexExpression>;

  template <typename T>
  using SimpleTypeOrCollection = AllowedArguments<T, std::shared_ptr<ValueArray<T>>>;

  using AnyCollectionArgument =
      AllowedArguments<BulkComplexExpression, std::shared_ptr<ValueArray<SupportedTypes>>...,
                       std::shared_ptr<ValueArray<Symbol>>, std::shared_ptr<CompoundArray>>;

  using AnySymbolicArgument =
      AllowedArguments<Symbol, BulkComplexExpression, std::shared_ptr<ValueArray<Symbol>>,
                       std::shared_ptr<CompoundArray>>;

  using TableArgument = AllowedArguments<std::shared_ptr<CompoundArray>>;

  // common visit dispatcher types

  using AnyTypeVisitDispatcher =
      BatchVisitDispatcher<SupportedTypes..., Symbol, BulkComplexExpression,
                           std::shared_ptr<ValueArray<SupportedTypes>>...,
                           std::shared_ptr<ValueArray<Symbol>>, std::shared_ptr<CompoundArray>>;

  using CollectionVisitDispatcher =
      BatchVisitDispatcher<std::shared_ptr<ValueArray<SupportedTypes>>...,
                           std::shared_ptr<ValueArray<Symbol>>, std::shared_ptr<CompoundArray>>;

  using SimpleTypeVisitDispatcher = BatchVisitDispatcher<SupportedTypes..., Symbol>;

  /// to iterate and evaluate on each element of a batch
  template <typename Func, typename... ArrayPtrIn>
  static BulkExpression evaluateElements(Func&& func, ArrayPtrIn&&... in) {
    auto apply = [&](auto& out, auto&&... inIt) {
      auto outIt = out.begin();
      for(; outIt != out.end(); ++outIt, ((++inIt), ...)) {
        *outIt = func((FromArrayTypeToElementType<
                       typename std::remove_reference_t<ArrayPtrIn>::element_type>)(*inIt)...);
      }
    };

    using ReturnType = ReturnType<std::decay_t<decltype(func)>, std::decay_t<decltype(in)>...>;
    if constexpr(std::is_same_v<ReturnType, Symbol>) {
      // assuming symbol to be always a single output
      // (different symbols must be dispatched to different batches!)
      auto output = std::make_shared<ValueArray<Symbol>>(1);
      apply(*output, in->begin()...);
      return output;
    } else if constexpr(std::is_same_v<ReturnType, ComplexExpression>) {
      auto output = std::make_shared<CompoundArray>();
      apply(*output, in->begin()...);
      return output;
    } else {
      auto output = std::make_shared<ValueArray<ReturnType>>();
      size_t outputSize = 1;
      (..., [&outputSize, &in]() { outputSize = std::max(outputSize, in->length()); }());
      output->resize(outputSize);
      apply(*output,
            const_cast<typename std::remove_reference_t<ArrayPtrIn>::element_type const*>(in.get())
                ->begin()...);
      return output;
    }
  }

  template <typename ArrayType> static BulkExpression getColumnNames(ArrayType const& srcArray) {
    // create a temporary Symbol array from the field names
    auto columnsPtr = std::make_shared<ValueArray<Symbol>>();
    auto& columns = *columnsPtr;
    auto const& batchData = srcArray.data();
    if(batchData.builder || !batchData.arrays.chunks().empty()) {
      auto type = batchData.builder ? batchData.builder->type() : batchData.arrays.chunk(0)->type();
      auto const& extensionType = *dynamic_cast<arrow::ExtensionType const*>(type.get());
      auto structType = extensionType.storage_type();
      columns.resize(structType->num_fields());
      auto columnIt = columns.begin();
      for(auto const& field : structType->fields()) {
        *columnIt = Symbol(field->name());
        ++columnIt;
      }
    }
    return columnsPtr;
  }

  template <typename DestArrayType, typename SrcArrayType>
  static void insertAllRows(DestArrayType& destArray, SrcArrayType const& srcArray) {
    std::vector<BatchData> argData;
    argData.reserve(srcArray.numArguments());
    for(auto srcArg : srcArray) {
      CollectionVisitDispatcher::visit(
          [&argData](auto const& srcColumnPtr) { argData.emplace_back(srcColumnPtr->data()); },
          srcArg);
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
      // check if the columns already exist in the destination
      // if not initialise the right arg batch types (but empty so far)
      if(destArray.numArguments() == 0) {
        std::vector<BatchData> argData;
        argData.reserve(srcArray.numArguments());
        for(auto srcArg : srcArray) {
          CollectionVisitDispatcher::visit(
              [&argData](auto const& srcColumnPtr) { argData.emplace_back(srcColumnPtr->data()); },
              srcArg);
        }
        destArray.initArguments(srcArray.getHead(), argData);
      }
      // then recursive call for every argument
      size_t prevSize = destArray.length();
      if(needResize) {
        size_t numRowsToInsert = rowIndices.size();
        destArray.resize(prevSize + numRowsToInsert);
      }
      auto destArgIt = destArray.begin();
      for(auto srcArg : srcArray) {
        CollectionVisitDispatcher::visit(
            [&destArgIt, &rowIndices, &prevSize](auto const& srcColumnPtr) {
              using ColumnType = std::decay_t<decltype(srcColumnPtr)>;
              // insert to existing arg column
              // assuming destination column is same type
              auto destArg = *destArgIt;
              BatchVisitDispatcher<ColumnType>::visit(
                  [&srcColumnPtr, &rowIndices, &prevSize](auto& destColumnPtr) {
                    auto& destColumn = *destColumnPtr;
                    insertRowValuesInOrder(destColumn, *srcColumnPtr, rowIndices, false, prevSize);
                  },
                  destArg);
              ++destArgIt;
              return;
            },
            srcArg);
      }
      return;
    }
    // general case
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
      // check if the columns already exist in the destination
      // if not initialise the right arg batch types (but empty so far)
      if(destArray.numArguments() == 0) {
        std::vector<BatchData> argData;
        argData.reserve(srcArray.numArguments());
        for(auto srcArg : srcArray) {
          CollectionVisitDispatcher::visit(
              [&argData](auto const& srcColumnPtr) { argData.emplace_back(srcColumnPtr->data()); },
              srcArg);
        }
        destArray.initArguments(srcArray.getHead(), argData);
      }
      // then recursive call for every argument
      size_t prevSize = destArray.length();
      if(needResize) {
        size_t numRowsToInsert = conditionArray.calculateBitCount();
        destArray.resize(numRowsToInsert);
      }
      auto destArgIt = destArray.begin();
      for(auto srcArg : srcArray) {
        CollectionVisitDispatcher::visit(
            [&destArgIt, &conditionArray, &prevSize](auto const& srcColumnPtr) {
              using ColumnType = std::decay_t<decltype(srcColumnPtr)>;
              // insert to existing arg column
              // assuming destination column is same type
              auto destArg = *destArgIt;
              BatchVisitDispatcher<ColumnType>::visit(
                  [&srcColumnPtr, &conditionArray, &prevSize](auto& destColumnPtr) {
                    auto& destColumn = *destColumnPtr;
                    insertRowValuesWithCondition(destColumn, *srcColumnPtr, conditionArray, false,
                                                 prevSize);
                  },
                  destArg);
              ++destArgIt;
              return;
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
  // to retrieve return type for a specific set of Batch argument types
  template <typename T> using FromArrayTypeToElementType = typename T::ValueType;
  template <typename Func, typename... ArrayPtrTypes>
  using ReturnType = typename std::invoke_result_t<
      Func, FromArrayTypeToElementType<typename ArrayPtrTypes::element_type>...>;
};

} // namespace boss::engines::bulk
