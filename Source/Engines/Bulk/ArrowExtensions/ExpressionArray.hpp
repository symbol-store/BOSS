#pragma once

#include "../Utils/CompareExpression.hpp"
#include "ComplexExpressionArray.hpp"
#include "IterableBuilders.hpp"
#include "SymbolArray.hpp"

#include "../../../Expression.hpp"
#include "../../../Utilities.hpp"

#include <arrow/array/array_binary.h>
#include <arrow/array/array_dict.h>
#include <arrow/array/array_nested.h>
#include <arrow/array/array_primitive.h>
#include <arrow/array/builder_binary.h>
#include <arrow/array/builder_dict.h>
#include <arrow/array/builder_primitive.h>
#include <arrow/array/builder_union.h>

#include <map>
#include <type_traits>

namespace boss::engines::bulk {

/** ExpressionArray is for storing an expression of any type.
 * It currently implements a dense union so each expression type
 * is dispatched to a different child array.*/
using ExpressionArray = arrow::DenseUnionArray;

// workaround to access offsets_builders in ExpressionArrayBuilder
// [https://github.com/symbol-store/BOSS/issues/88]
// clean up usage of arrow API to avoid issues for accessing private members.
template <typename Tag, typename Tag::type PrivateMember> struct AccessPrivateMember {
  friend typename Tag::type get(Tag /*unused*/) { return PrivateMember; }
};
struct DenseUnionBuilder_member {
  typedef arrow::TypedBufferBuilder<int32_t> arrow::DenseUnionBuilder::*type;
  friend type get(DenseUnionBuilder_member /*unused*/);
};
template struct AccessPrivateMember<DenseUnionBuilder_member,
                                    &arrow::DenseUnionBuilder::offsets_builder_>;

/** This builder is implementing convenience functions to append expression to a dense union:
 * - creating a child buidler whose class corresponding to a specific expression type
 * - mapping the expression type to a child builder in which to append the expression
 * - checking if an expression type is already supported by any of th existing child builder */
class ExpressionArrayBuilder : public arrow::DenseUnionBuilder {
public:
  explicit ExpressionArrayBuilder(arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::DenseUnionBuilder(pool) {}

  // [https://github.com/symbol-store/BOSS/issues/92]
  /// Check if the expression type is already handled by this union array builder
  /// or if it will require to create a new array type
  bool IsSupported(Expression const& expr) {
    return m_expressionToType.find(expr) != m_expressionToType.end();
  }

  // [https://github.com/symbol-store/BOSS/issues/92]
  /// Check if the expression type is already handled by this union array builder
  /// or if it will require to create a new array type
  bool IsSupported(arrow::DataType const& type) {
    for(int i = 0; i < num_children(); ++i) {
      auto builder = child_builder(i);
      auto supportedType = child_builder(i)->type();
      if(type.id() != supportedType->id()) {
        continue;
      }
      if(type.num_fields() != supportedType->num_fields()) {
        continue;
      }
      bool match = true;
      auto fieldsIt = supportedType->fields().begin();
      for(auto const& field : type.fields()) {
        if(!field->Equals(*fieldsIt)) {
          match = false;
          break;
        }
      }
      if(match) {
        return true;
      }
    }
    return false;
  }

  /// dispatch the expression to the corresponding child builder
  /// and append to both the union builder and the child builder
  arrow::Status AppendExpression(Expression const& expr) {
    auto it = m_expressionToType.find(expr);
    std::shared_ptr<ArrayBuilder> childBuilder;
    bool foundInCache = (it != m_expressionToType.end());
    auto& cachedTypeId = foundInCache ? it->second : m_expressionToType[expr];
    if(foundInCache) {
      // retrieve from the cache
      childBuilder = child_builder(cachedTypeId);
    } else {
      // This type of expression is not supported yet
      // Append a new array for this expression
      // and cache the id for next time
      childBuilder = makeChildBuilder(expr);
      cachedTypeId = AppendChild(childBuilder, createFieldName(expr));
    }
    auto status = Append(cachedTypeId);
    if(!status.ok()) {
      return status;
    }
    return appendToChildBuilder(expr, childBuilder);
  }

  /// append a full array at once
  /// dispatching to the child builder based on the type of the source array
  arrow::Status AppendExpressions(std::shared_ptr<arrow::Array> const& exprArrayPtr) {
    // [https://github.com/symbol-store/BOSS/issues/88] more proper type handling of type
    if(exprArrayPtr->type_id() != arrow::Type::DENSE_UNION) {
      // this is not an expression array
      // merge it as a normal batch array
      auto const& srcArray = *exprArrayPtr;
      auto const& srcType = srcArray.type();
      auto destType = findOrCreateBuilder(*srcType);
      auto const& destBuilder = child_builder(destType);
      auto curLength = destBuilder->length();

      // append type/offsets to the union array
      auto typeStatus = types_builder_.Append(srcArray.length(), destType);
      if(!typeStatus.ok()) {
        return typeStatus;
      }
      for(int i = curLength; i < curLength + srcArray.length(); ++i) {
        auto offsetStatus = offsets_builder().Append(i);
        if(!offsetStatus.ok()) {
          return offsetStatus;
        }
      }

      // append to the child builder
      auto childStatus = appendToChildBuilder(srcArray, destBuilder);
      if(!childStatus.ok()) {
        return childStatus;
      }

      return arrow::Status::OK();
    }

    // handle each union child array separately
    // (we lose the order)
    auto const& exprArray = dynamic_cast<ExpressionArray const&>(*exprArrayPtr);
    for(int i = 0; i < exprArray.num_fields(); ++i) {
      auto const& srcArrayPtr = exprArray.field(i);
      auto childStatus = AppendExpressions(srcArrayPtr);
      if(!childStatus.ok()) {
        return childStatus;
      }
    }

    return arrow::Status::OK();
  }

  /// append a full builder at once
  /// dispatching to the child builder based on the type of the source builder
  /// logicalSize: real builder size, needed for the builder until we can support shrinking builders
  arrow::Status AppendExpressions(std::shared_ptr<arrow::ArrayBuilder> const& exprArrayBuilderPtr,
                                  size_t logicalSize) {
    // [https://github.com/symbol-store/BOSS/issues/88] more proper type handling of type
    if(exprArrayBuilderPtr->type()->id() != arrow::Type::DENSE_UNION) {
      // this is not an expression array builder
      // merge it as a normal batch array builder
      auto const& srcBuilder = *exprArrayBuilderPtr;
      auto const& srcType = srcBuilder.type();
      auto destType = findOrCreateBuilder(*srcType);
      auto const& destBuilder = child_builder(destType);
      auto curLength = destBuilder->length();

      auto typeStatus = types_builder_.Append(logicalSize, destType);
      if(!typeStatus.ok()) {
        return typeStatus;
      }
      for(int i = curLength; i < curLength + logicalSize; ++i) {
        auto offsetStatus = offsets_builder().Append(i);
        if(!offsetStatus.ok()) {
          return offsetStatus;
        }
      }

      // append to the child builder
      auto childStatus = appendToChildBuilder(srcBuilder, logicalSize, destBuilder);
      if(!childStatus.ok()) {
        return childStatus;
      }

      return arrow::Status::OK();
    }

    // handle each union child array separately
    // (we lose the order)
    auto const& exprArrayBuilder =
        dynamic_cast<ExpressionArrayBuilder const&>(*exprArrayBuilderPtr);
    for(int i = 0; i < exprArrayBuilder.num_children(); ++i) {
      auto const& srcArrayBuilderPtr = exprArrayBuilder.child_builder(i);
      auto childStatus = AppendExpressions(srcArrayBuilderPtr, logicalSize);
      if(!childStatus.ok()) {
        return childStatus;
      }
    }

    return arrow::Status::OK();
  }

  /// prepare the array for a specific type, but don't append anything yet
  void CopyFields(std::shared_ptr<arrow::DataType> const& type) {
    // [https://github.com/symbol-store/BOSS/issues/88] more proper type handling of type
    if(type->id() != arrow::Type::DENSE_UNION) {
      // this is not an expression array builder
      // merge it as a normal batch array builder
      findOrCreateBuilder(*type);
      return;
    }

    // handle each union child array separately
    // (we lose the order)
    for(auto const& field : type->fields()) {
      CopyFields(field->type());
    }
  }

private:
  arrow::TypedBufferBuilder<int32_t>& offsets_builder() {
    return this->*get(DenseUnionBuilder_member());
  }

  using ComplexExpressionArrayBuilder = ComplexExpressionArrayBuilder<ExpressionArrayBuilder>;

  std::map<Expression, int8_t, CompareExpression<true, false>> m_expressionToType;

  int8_t findOrCreateBuilder(arrow::DataType const& srcType) {
    for(int i = 0; i < children_.size(); ++i) {
      auto const& childType = children_[i]->type();
      if(childType->Equals(srcType)) {
        return type_codes_[i];
      }
    }

    // not found, create new builder
    auto newBuilder = makeChildBuilder(srcType);
    // TODO: find a way to update m_expressionToType
    // for now, findOrCreateBuilder should not be used together with AppendExpression!
    return AppendChild(newBuilder);
  }

  std::shared_ptr<arrow::ArrayBuilder> makeChildBuilder(Expression const& expr) {
    return std::visit(boss::utilities::overload(
                          [&](bool /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
                            return std::make_shared<IterableBooleanBuilder>(pool_);
                          },
                          [&](int /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
                            return std::make_shared<IterableInt32Builder>(pool_);
                          },
                          [&](float /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
                            return std::make_shared<IterableFloatBuilder>(pool_);
                          },
                          [&](std::string const& /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
                            return std::make_shared<IterableStringBuilder<>>(pool_);
                          },
                          [&](Symbol const& /*s*/) -> std::shared_ptr<arrow::ArrayBuilder> {
                            return std::make_shared<SymbolArrayBuilder>(pool_);
                          },
                          [&](ComplexExpression const& e) -> std::shared_ptr<arrow::ArrayBuilder> {
                            return std::make_shared<ComplexExpressionArrayBuilder>(
                                e.getHead(), e.getArguments().size(), pool_);
                          }),
                      expr);
  }

  std::shared_ptr<arrow::ArrayBuilder> makeChildBuilder(arrow::DataType const& type) {
    switch(type.id()) {
    case arrow::Type::BOOL:
      return std::make_shared<IterableBooleanBuilder>(pool_);
    case arrow::Type::INT32:
      return std::make_shared<IterableInt32Builder>(pool_);
    case arrow::Type::FLOAT:
      return std::make_shared<IterableFloatBuilder>(pool_);
    case arrow::Type::STRING:
      return std::make_shared<IterableStringBuilder<>>(pool_);
    case arrow::Type::EXTENSION: {
      auto const& extensionType = dynamic_cast<arrow::ExtensionType const&>(type);
      if(extensionType.extension_name()[0] == 's') {
        // SYMBOL
        return std::make_shared<SymbolArrayBuilder>(pool_);
      }
      // EXPRESSION
      auto const& complexType =
          dynamic_cast<ComplexExpressionArray::ComplexExpressionArrayType const&>(extensionType);
      auto storageType = complexType.storage_type();
      auto newBuilderPtr = std::make_shared<ComplexExpressionArrayBuilder>(
          complexType.getHead(), storageType->num_fields(), pool_);

      std::vector<std::shared_ptr<arrow::DataType>> childTypes;
      childTypes.reserve(storageType->num_fields());
      for(auto const& field : storageType->fields()) {
        childTypes.emplace_back(field->type());
      }
      auto status = newBuilderPtr->initArguments(childTypes);
      if(status.ok()) {
        return newBuilderPtr;
      }
    }

    default:
      break;
    }
    return nullptr;
  }

  static std::string createFieldName(Expression const& expr) {
    return std::visit(boss::utilities::overload(
                          [&](bool /*v*/) -> std::string { return "Bool"; },
                          [&](int /*v*/) -> std::string { return "Int"; },
                          [&](float /*v*/) -> std::string { return "Float"; },
                          [&](std::string const& /*v*/) -> std::string { return "String"; },
                          [&](Symbol const& /*s*/) -> std::string { return "Symbol"; },
                          [&](ComplexExpression const& e) { return e.getHead().getName(); }),
                      expr);
  }

  static arrow::Status
  appendToChildBuilder(Expression const& expr,
                       std::shared_ptr<arrow::ArrayBuilder> const& childBuilder) {
    return std::visit(
        boss::utilities::overload(
            [&](bool v) { return dynamic_cast<IterableBooleanBuilder&>(*childBuilder).Append(v); },
            [&](int v) { return dynamic_cast<IterableInt32Builder&>(*childBuilder).Append(v); },
            [&](float v) { return dynamic_cast<IterableFloatBuilder&>(*childBuilder).Append(v); },
            [&](std::string const& v) {
              return dynamic_cast<IterableStringBuilder<>&>(*childBuilder).Append(v);
            },
            [&](Symbol const& s) {
              return dynamic_cast<SymbolArrayBuilder&>(*childBuilder).Append(s);
            },
            [&](ComplexExpression const& e) {
              return dynamic_cast<ComplexExpressionArrayBuilder&>(*childBuilder)
                  .AppendExpression(e);
            }),
        expr);
  }

  static arrow::Status
  appendToChildBuilder(arrow::Array const& srcArray,
                       std::shared_ptr<arrow::ArrayBuilder> const& destBuilder) {
    auto const& type = srcArray.type();
    switch(type->id()) {
    case arrow::Type::BOOL: {
      auto const& typedSrcArray = dynamic_cast<arrow::BooleanArray const&>(srcArray);
      auto const& srcValues = *typedSrcArray.values();
      return dynamic_cast<IterableBooleanBuilder&>(*destBuilder)
          .AppendValues(srcValues.data() + typedSrcArray.data()->offset, typedSrcArray.length());
    }
    case arrow::Type::INT32: {
      auto const& typedSrcArray = dynamic_cast<arrow::Int32Array const&>(srcArray);
      return dynamic_cast<IterableInt32Builder&>(*destBuilder)
          .AppendValues(typedSrcArray.raw_values(), typedSrcArray.length());
    }
    case arrow::Type::FLOAT: {
      auto const& typedSrcArray = dynamic_cast<arrow::FloatArray const&>(srcArray);
      return dynamic_cast<IterableFloatBuilder&>(*destBuilder)
          .AppendValues(typedSrcArray.raw_values(), typedSrcArray.length());
    }
    case arrow::Type::STRING: {
      auto const& typedSrcArray = dynamic_cast<arrow::StringArray const&>(srcArray);
      auto const& valueData = *typedSrcArray.value_data();
      auto& typedDestBuilder = dynamic_cast<IterableStringBuilder<>&>(*destBuilder);
      auto reserveStatus = typedDestBuilder.Reserve(typedSrcArray.length());
      if(!reserveStatus.ok()) {
        return reserveStatus;
      }
      auto reserveDataStatus = typedDestBuilder.ReserveData(valueData.size());
      if(!reserveDataStatus.ok()) {
        return reserveDataStatus;
      }
      for(size_t i = 0; i < typedSrcArray.length(); ++i) {
        typedDestBuilder.UnsafeAppend(typedSrcArray.GetView(i));
      }
      return arrow::Status::OK();
    }
    case arrow::Type::EXTENSION: {
      auto const& extensionType = dynamic_cast<arrow::ExtensionType const&>(*type);
      if(extensionType.extension_name()[0] == 's') {
        // SYMBOL
        auto const& typedSrcArray = dynamic_cast<SymbolArray const&>(srcArray);
        // handle same as for a string
        auto const& valueData = *typedSrcArray.value_data();
        auto& typedDestBuilder = dynamic_cast<SymbolArrayBuilder&>(*destBuilder);
        auto reserveStatus = typedDestBuilder.Reserve(typedSrcArray.length());
        if(!reserveStatus.ok()) {
          return reserveStatus;
        }
        auto reserveDataStatus = typedDestBuilder.ReserveData(valueData.size());
        if(!reserveDataStatus.ok()) {
          return reserveDataStatus;
        }
        for(size_t i = 0; i < typedSrcArray.length(); ++i) {
          typedDestBuilder.UnsafeAppend(typedSrcArray.GetView(i));
        }
        return arrow::Status::OK();
      }
      // EXPRESSION
      auto const& typedSrcArray = dynamic_cast<ComplexExpressionArray const&>(srcArray);
      return dynamic_cast<ComplexExpressionArrayBuilder&>(*destBuilder)
          .AppendExpressions(typedSrcArray);
    }

    default:
      break;
    }

    return arrow::Status::TypeError("source array type not supported");
  }

  static arrow::Status
  appendToChildBuilder(arrow::ArrayBuilder const& srcBuilder, size_t srcLogicalSize,
                       std::shared_ptr<arrow::ArrayBuilder> const& destBuilder) {
    auto const& type = srcBuilder.type();
    switch(type->id()) {
    case arrow::Type::BOOL: {
      auto const& typedSrcBuilder = dynamic_cast<IterableBooleanBuilder const&>(srcBuilder);
      auto const& srcValues = typedSrcBuilder.raw_values();
      return dynamic_cast<IterableBooleanBuilder&>(*destBuilder)
          .AppendValues(srcValues, srcLogicalSize);
    }
    case arrow::Type::INT32: {
      auto const& typedSrcBuilder = dynamic_cast<IterableInt32Builder const&>(srcBuilder);
      auto const& srcValues = typedSrcBuilder.raw_values();
      return dynamic_cast<IterableInt32Builder&>(*destBuilder)
          .AppendValues(srcValues, srcLogicalSize);
    }
    case arrow::Type::FLOAT: {
      auto const& typedSrcBuilder = dynamic_cast<IterableFloatBuilder const&>(srcBuilder);
      auto const& srcValues = typedSrcBuilder.raw_values();
      return dynamic_cast<IterableFloatBuilder&>(*destBuilder)
          .AppendValues(srcValues, srcLogicalSize);
    }
    case arrow::Type::STRING: {
      auto const& typedSrcBuilder = dynamic_cast<IterableStringBuilder<> const&>(srcBuilder);
      auto const& valueDataLength = typedSrcBuilder.value_data_length();
      auto& typedDestBuilder = dynamic_cast<IterableStringBuilder<>&>(*destBuilder);
      auto reserveStatus = typedDestBuilder.Reserve(srcLogicalSize);
      if(!reserveStatus.ok()) {
        return reserveStatus;
      }
      auto reserveDataStatus = typedDestBuilder.ReserveData(valueDataLength);
      if(!reserveDataStatus.ok()) {
        return reserveDataStatus;
      }
      for(size_t i = 0; i < srcLogicalSize; ++i) {
        typedDestBuilder.UnsafeAppend(typedSrcBuilder.GetView(i));
      }
      return arrow::Status::OK();
    }
    case arrow::Type::EXTENSION: {
      auto const& extensionType = dynamic_cast<arrow::ExtensionType const&>(*type);
      if(extensionType.extension_name()[0] == 's') {
        // SYMBOL
        auto const& typedSrcBuilder = dynamic_cast<SymbolArrayBuilder const&>(srcBuilder);
        // handle same as for a string
        auto const& valueDataLength = typedSrcBuilder.value_data_length();
        auto& typedDestBuilder = dynamic_cast<SymbolArrayBuilder&>(*destBuilder);
        auto reserveStatus = typedDestBuilder.Reserve(srcLogicalSize);
        if(!reserveStatus.ok()) {
          return reserveStatus;
        }
        auto reserveDataStatus = typedDestBuilder.ReserveData(valueDataLength);
        if(!reserveDataStatus.ok()) {
          return reserveDataStatus;
        }
        for(size_t i = 0; i < srcLogicalSize; ++i) {
          typedDestBuilder.UnsafeAppend(typedSrcBuilder.GetView(i));
        }
        return arrow::Status::OK();
      }
      // EXPRESSION
      auto const& typedSrcBuilder = dynamic_cast<ComplexExpressionArrayBuilder const&>(srcBuilder);
      return dynamic_cast<ComplexExpressionArrayBuilder&>(*destBuilder)
          .AppendExpressions(typedSrcBuilder, srcLogicalSize);
    }

    default:
      break;
    }

    return arrow::Status::TypeError("source array type not supported");
  }
};

} // namespace boss::engines::bulk
