
#pragma once

#include "../BatchData.hpp"

#include "../../../Expression.hpp"

#include <arrow/array/array_nested.h>
#include <arrow/array/builder_nested.h>
#include <arrow/chunked_array.h>
#include <arrow/extension_type.h>

#include <string>
#include <vector>

namespace boss::engines::bulk {

/** We use ComplexExpressionArray to store complex expressions.
 * This is an extension of the StructArray.
 * The children array are the arguments.
 * We store the head symbol as a metadata to the custom type. */
class ComplexExpressionArray : public arrow::StructArray {
public:
  explicit ComplexExpressionArray(std::shared_ptr<arrow::ArrayData> const& data, Symbol const& head)
      : arrow::StructArray(data) {
    // make sure to set back the extension type after the end of call from base array class
    auto adjustedData = data->Copy();
    adjustedData->type = std::make_shared<ComplexExpressionArrayType>(head, data->type->fields());
    SetData(adjustedData);
  }

  /** Custom type to implement an Arrow array for complex expressions.
   * This is mostly boilerplate code to be compliant with Arrow.
   * It also stores an additional metadata for the head symbol. */
  class ComplexExpressionArrayType : public arrow::ExtensionType {
  public:
    explicit ComplexExpressionArrayType(Symbol const& head, arrow::FieldVector const& fields)
        : ExtensionType(arrow::struct_(fields)), m_head(head) {}

    Symbol const& getHead() const { return m_head; }

    /// Called by Arrow to create our custom ComplexExpressionArray from a
    /// ComplexExpressionArrayBuilder
    std::shared_ptr<arrow::Array> MakeArray(std::shared_ptr<arrow::ArrayData> data) const override {
      // temporarly change to the underline type for the construction
      // it will be reverted in the ComplexExpressionArray constructor
      auto adjustedData = data->Copy();
      adjustedData->type = arrow::struct_(storage_type()->fields());
      return std::make_shared<ComplexExpressionArray>(adjustedData, m_head);
    }

    ///////////////////////////////////////////////////////////////////////
    // code required by Arrow to implement an extension type
    std::string extension_name() const override { return "complex-expr-type"; }
    bool ExtensionEquals(ExtensionType const& other) const override {
      auto const& other_ext = static_cast<ExtensionType const&>(other);
      if(other_ext.extension_name() != this->extension_name()) {
        return false;
      }
      return this->getHead() == dynamic_cast<ComplexExpressionArrayType const&>(other).getHead();
    }
    arrow::Result<std::shared_ptr<DataType>>
    Deserialize(std::shared_ptr<DataType> storage_type,
                std::string const& serialized) const override {
      return std::make_shared<ComplexExpressionArrayType>(Symbol(serialized),
                                                          storage_type->fields());
    }
    std::string Serialize() const override { return m_head.getName(); }
    ///////////////////////////////////////////////////////////////////////

  private:
    Symbol m_head;
  };
};

/** ExpressionArrayBuilder is the builder corresponding to ComplexExpressionArray.
 * Because it uses custom type ComplexExpressionArrayType, Arrow will automatically
 * pick it up to create ComplexExpressionArray when finishing the builder.*/
template <typename ExpressionArrayBuilder>
class ComplexExpressionArrayBuilder : public arrow::StructBuilder {
public:
  // Arrow requires to register custom extension types.
  // So we make sure to do this the first time we need it.
  static void initialisation() {
    static bool initialised = false;
    if(!initialised) {
      auto status = arrow::RegisterExtensionType(
          std::make_shared<ComplexExpressionArray::ComplexExpressionArrayType>(
              Symbol(""), arrow::FieldVector{}));
      if(!status.ok()) {
        return;
      }
      initialised = true;
    }
  }

  ComplexExpressionArrayBuilder(Symbol const& head, arrow::FieldVector const& fields,
                                arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StructBuilder(std::make_shared<arrow::StructType>(fields), pool,
                             makeChildBuilders(fields.size(), pool)),
        m_head(head) {
    initialisation();
  }

  ComplexExpressionArrayBuilder(Symbol const& head, size_t argCount,
                                arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StructBuilder(std::make_shared<arrow::StructType>(makeFields(argCount)), pool,
                             makeChildBuilders(argCount, pool)),
        m_head(head) {
    initialisation();
  }

  ComplexExpressionArrayBuilder(ComplexExpressionArrayBuilder const& other, bool clear = false)
      : arrow::StructBuilder(
            other.arrow::StructBuilder::type(), other.pool_,
            clear ? makeChildBuilders(other.arrow::StructBuilder::type()->num_fields(), other.pool_)
                  : other.children_),
        m_head(other.m_head) {
    initialisation();
    // initialise properly the struct builder
    // since the children arrays weren't empty but not handle by StructBuilder constructor
    if(!children_.empty()) {
      auto length = children_[0]->length();
      auto reserveStatus = Reserve(length);
      if(!reserveStatus.ok()) {
        return;
      }
      UnsafeAppendToBitmap(length, true);
    }
  }

  ComplexExpressionArrayBuilder(ComplexExpressionArrayBuilder&& other, bool clear = false) noexcept
      : arrow::StructBuilder(
            other.arrow::StructBuilder::type(), other.pool_,
            clear ? makeChildBuilders(other.arrow::StructBuilder::type()->num_fields(), other.pool_)
                  : other.children_),
        m_head(other.m_head) {
    initialisation();
    // initialise properly the struct builder
    // since the children arrays wreen't empty but not handle by StructBuilder constructor
    if(!children_.empty()) {
      auto length = children_[0]->length();
      auto reserveStatus = Reserve(length);
      if(!reserveStatus.ok()) {
        return;
      }
      UnsafeAppendToBitmap(length, true);
    }
  }

  ~ComplexExpressionArrayBuilder() override = default;
  ComplexExpressionArrayBuilder& operator=(ComplexExpressionArrayBuilder const& other) = delete;
  ComplexExpressionArrayBuilder& operator=(ComplexExpressionArrayBuilder&& other) = delete;

  Symbol const& getHead() { return m_head; }

  std::shared_ptr<arrow::DataType> type() const override {
    return std::make_shared<ComplexExpressionArray::ComplexExpressionArrayType>(
        m_head, arrow::StructBuilder::type()->fields());
  }

  // Usually when resizing a struct array, we need to resize both the struct array itself
  // and the children arrays. This function only resize the struct array itself..
  // For now, the children array have to be extracted as Batch and resized manually afterwards.
  // [https://github.com/symbol-store/BOSS/issues/88]
  // We need to do that until we have a more robust arrow array API.
  // This is because we don't have yet a consistent method to resize the children,
  // so it has to be done through each of the children's batch resize() call.
  arrow::Status resizeStructArray(size_t size) {
    if(size < length()) {
      // TODO: do we need to support that case?
      return arrow::Status::OK();
    }
    return AppendToBitmap(size - length(), true);
  }

  // [https://github.com/symbol-store/BOSS/issues/92]
  /// Check if the expression type of each argument
  /// is already supported by the child union array builder
  /// or if it will require to create a new array type
  bool IsSupported(ComplexExpression const& expr) {
    for(int idx = 0; idx < expr.getArguments().size(); ++idx) {
      auto& argBuilder = dynamic_cast<ExpressionArrayBuilder&>(*child_builder(idx));
      if(!argBuilder.IsSupported(expr.getArguments()[idx])) {
        return false;
      }
    }
    return true;
  }

  // [https://github.com/symbol-store/BOSS/issues/92]
  /// Check if the expression type of each argument
  /// is already supported by the child union array builder
  /// or if it will require to create a new array type
  bool IsSupported(std::vector<BatchData> const& argData) {
    for(int idx = 0; idx < argData.size(); ++idx) {
      auto& argBuilder = dynamic_cast<ExpressionArrayBuilder&>(*child_builder(idx));
      auto type = argData[idx].builder->type();
      if(!argBuilder.IsSupported(*type)) {
        return false;
      }
    }
    return true;
  }

  // [https://github.com/symbol-store/BOSS/issues/88]
  // refactor all the AppendExpression once we have a better array API
  arrow::Status AppendExpression(ComplexExpression const& expr) {
    // append to the args structure
    auto structStatus = Append();
    if(!structStatus.ok()) {
      return structStatus;
    }

    // append each argument
    for(int idx = 0; idx < expr.getArguments().size(); ++idx) {
      auto& argBuilder = dynamic_cast<ExpressionArrayBuilder&>(*child_builder(idx));
      auto status = argBuilder.AppendExpression(expr.getArguments()[idx]);
      if(!status.ok()) {
        return status;
      }
    }

    return arrow::Status::OK();
  };

  // [https://github.com/symbol-store/BOSS/issues/88]
  // refactor all the AppendExpression once we have a better array API
  arrow::Status AppendExpressions(std::vector<BatchData> const& argData) {
    if(argData.empty()) {
      // no arguments?
      return arrow::Status::OK();
    }

    // assuming same length for every argument array
    auto length = argData[0].arrays.length() + argData[0].builderLogicalSize;

    // append to the args structure
    auto structStatus = Reserve(length);
    if(!structStatus.ok()) {
      return structStatus;
    }
    UnsafeAppendToBitmap(length, true);

    // append each argument
    for(int idx = 0; idx < argData.size(); ++idx) {
      auto& argBuilder = dynamic_cast<ExpressionArrayBuilder&>(*child_builder(idx));

      // append each chunk
      for(auto const& chunk : argData[idx].arrays.chunks()) {
        auto status = argBuilder.AppendExpressions(chunk);
        if(!status.ok()) {
          return status;
        }
      }

      // append builder info
      if(argData[idx].builder && argData[idx].builderLogicalSize > 0) {
        auto status =
            argBuilder.AppendExpressions(argData[idx].builder, argData[idx].builderLogicalSize);
        if(!status.ok()) {
          return status;
        }
      }
    }

    return arrow::Status::OK();
  }

  // [https://github.com/symbol-store/BOSS/issues/88]
  // refactor all the AppendExpression once we have a better array API
  arrow::Status AppendExpressions(ComplexExpressionArray const& complexArray) {
    auto length = complexArray.length();

    // append to the args structure
    auto structStatus = Reserve(length);
    if(!structStatus.ok()) {
      return structStatus;
    }
    UnsafeAppendToBitmap(length, true);

    // append each argument
    // (assuming matching order between source and destination)
    int destIndex = 0;
    for(auto const& srcArgArrayPtr : complexArray.fields()) {
      auto& argBuilder = dynamic_cast<ExpressionArrayBuilder&>(*child_builder(destIndex++));
      auto status = argBuilder.AppendExpressions(srcArgArrayPtr);
      if(!status.ok()) {
        return status;
      }
    }

    return arrow::Status::OK();
  }

  // [https://github.com/symbol-store/BOSS/issues/88]
  // refactor all the AppendExpression once we have a better array API
  arrow::Status AppendExpressions(ComplexExpressionArrayBuilder const& complexArrayBuilder,
                                  size_t logicalSize) {
    // append to the args structure
    auto structStatus = Reserve(logicalSize);
    if(!structStatus.ok()) {
      return structStatus;
    }
    UnsafeAppendToBitmap(logicalSize, true);

    // append each argument
    // (assuming matching order between source and destination)
    int destIndex = 0;
    for(int i = 0; i < complexArrayBuilder.num_children(); ++i) {
      auto const& srcArgBuilderPtr = complexArrayBuilder.child_builder(i);
      auto& argBuilder = dynamic_cast<ExpressionArrayBuilder&>(*child_builder(destIndex++));
      auto status = argBuilder.AppendExpressions(srcArgBuilderPtr, logicalSize);
      if(!status.ok()) {
        return status;
      }
    }

    return arrow::Status::OK();
  }

  /// prepare the array to receive the argument types, but not appending anything yet
  arrow::Status initArguments(std::vector<std::shared_ptr<arrow::DataType>> const& types) {
    if(types.empty()) {
      // no arguments?
      return arrow::Status::OK();
    }

    // append each argument
    for(int idx = 0; idx < types.size(); ++idx) {
      auto& argBuilder = dynamic_cast<ExpressionArrayBuilder&>(*child_builder(idx));
      argBuilder.CopyFields(types[idx]);
    }

    return arrow::Status::OK();
  }

private:
  Symbol m_head;

  static arrow::FieldVector makeFields(size_t argCount) {
    arrow::FieldVector fields;
    fields.reserve(argCount);
    for(size_t i = 1; i <= argCount; ++i) {
      fields.push_back(std::make_shared<arrow::Field>("", nullptr));
    }
    return fields;
  }

  static std::vector<std::shared_ptr<arrow::ArrayBuilder>>
  makeChildBuilders(size_t argCount, arrow::MemoryPool* pool) {
    std::vector<std::shared_ptr<arrow::ArrayBuilder>> argBuilders;
    argBuilders.reserve(argCount);
    for(size_t i = 1; i <= argCount; ++i) {
      auto argBuilder = std::make_shared<ExpressionArrayBuilder>(pool);
      argBuilders.push_back(argBuilder);
    }

    return argBuilders;
  }
};

} // namespace boss::engines::bulk
