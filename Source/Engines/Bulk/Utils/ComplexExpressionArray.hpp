
#pragma once

#include "BatchData.hpp"

#include "../../../Expression.hpp"

#include <arrow/array/array_nested.h>
#include <arrow/array/builder_nested.h>
#include <arrow/chunked_array.h>
#include <arrow/extension_type.h>

#include <string>
#include <vector>

namespace boss::engines::bulk {

class ComplexExpressionArray : public arrow::StructArray {
public:
  class ComplexExpressionArrayType : public arrow::ExtensionType {
  public:
    explicit ComplexExpressionArrayType(Symbol const& head,
                                        std::vector<std::shared_ptr<arrow::Field>> const& fields)
        : ExtensionType(arrow::struct_(fields)), m_head(head) {}

    Symbol const& getHead() const { return m_head; }

    std::string extension_name() const override { return "complex-expr-type"; }
    // what is that?

    bool ExtensionEquals(ExtensionType const& other) const override {
      // ???
      auto const& other_ext = static_cast<ExtensionType const&>(other);
      if(other_ext.extension_name() != this->extension_name()) {
        return false;
      }
      return this->getHead() == dynamic_cast<ComplexExpressionArrayType const&>(other).getHead();
    }

    std::shared_ptr<arrow::Array> MakeArray(std::shared_ptr<arrow::ArrayData> data) const override {
      // why and when is this needed?
      // temporarly change to the underline type for the construction
      // it will be reverted in the ComplexExpressionArray constructor
      auto adjustedData = data->Copy();
      adjustedData->type = arrow::struct_(storage_type()->fields());
      return std::make_shared<ComplexExpressionArray>(adjustedData, m_head);
    }

    arrow::Result<std::shared_ptr<DataType>>
    Deserialize(std::shared_ptr<DataType> storage_type,
                std::string const& serialized) const override {
      return std::make_shared<ComplexExpressionArrayType>(Symbol(serialized),
                                                          storage_type->fields());
    }

    std::string Serialize() const override { return m_head.getName(); }
    // why is this called?

  private:
    Symbol m_head;
  };

  explicit ComplexExpressionArray(std::shared_ptr<arrow::ArrayData> const& data, Symbol const& head)
      : arrow::StructArray(data) {
    // make sure to set back the extension type after the end of call from base array class
    auto adjustedData = data->Copy();
    adjustedData->type = std::make_shared<ComplexExpressionArrayType>(head, data->type->fields());
    SetData(adjustedData);
  }
};

template <typename ExpressionArrayBuilder>
class ComplexExpressionArrayBuilder : public arrow::StructBuilder {
  // I think we should separate arrow stuff & utilities more clearly from core functionality
public:
  static void initialisation() {
    // ???
    static bool initialised = false;
    if(!initialised) {
      auto status = arrow::RegisterExtensionType(
          std::make_shared<ComplexExpressionArray::ComplexExpressionArrayType>(
              Symbol(""), std::vector<std::shared_ptr<arrow::Field>>{}));
      if(!status.ok()) {
        return;
      }
      initialised = true;
    }
  }

  ComplexExpressionArrayBuilder(Symbol const& head, std::vector<std::string> const& columns,
                                arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StructBuilder(std::make_shared<arrow::StructType>(makeFields(columns)), pool,
                             makeChildBuilders(columns.size(), pool)),
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
      : arrow::StructBuilder(other.type(), other.pool_,
                             clear ? makeChildBuilders(other.type()->num_fields(), pool_)
                                   : other.children_),
        m_head(other.m_head) {
    initialisation();
  }

  ComplexExpressionArrayBuilder(ComplexExpressionArrayBuilder&& other, bool clear = false) noexcept
      : arrow::StructBuilder(other.type(), other.pool_,
                             clear ? makeChildBuilders(other.type()->num_fields(), pool_)
                                   : other.children_),
        m_head(other.m_head) {
    initialisation();
  }

  ~ComplexExpressionArrayBuilder() override = default;
  ComplexExpressionArrayBuilder& operator=(ComplexExpressionArrayBuilder const& other) = delete;
  ComplexExpressionArrayBuilder& operator=(ComplexExpressionArrayBuilder&& other) = delete;

  Symbol const& getHead() { return m_head; }

  std::shared_ptr<arrow::DataType> type() const override {
    return std::make_shared<ComplexExpressionArray::ComplexExpressionArrayType>(
        m_head, arrow::StructBuilder::type()->fields());
  }

  // this doesn't resize the children!
  // they have to be extracted and resize manually afterwards!
  arrow::Status resizeStructArray(size_t size) {
    // what children? Why do they need resizing?
    if(size < length()) {
      // TODO: do we need to support that case?
      return arrow::Status::OK();
    }
    return AppendToBitmap(size - length(), true);
  }

  bool IsSupported(ComplexExpression const& expr) {
    // by whom?
    for(int idx = 0; idx < expr.getArguments().size(); ++idx) {
      auto& argBuilder = dynamic_cast<ExpressionArrayBuilder&>(*child_builder(idx));
      if(!argBuilder.IsSupported(expr.getArguments()[idx])) {
        return false;
      }
    }
    return true;
  }

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
      auto status =
          argBuilder.AppendExpressions(argData[idx].builder, argData[idx].builderLogicalSize);
      if(!status.ok()) {
        return status;
      }
    }

    return arrow::Status::OK();
  }

  arrow::Status AppendExpressions(ComplexExpressionArray const& complexArray) {
    // can't we refactor some of the AppendExpressions functions?
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

  static std::vector<std::shared_ptr<arrow::Field>> makeFields(size_t argCount) {
    std::vector<std::shared_ptr<arrow::Field>> fields;
    fields.reserve(argCount);
    for(size_t i = 1; i <= argCount; ++i) {
      fields.push_back(std::make_shared<arrow::Field>("arg" + std::to_string(i), nullptr));
    }
    return fields;
  }

  static std::vector<std::shared_ptr<arrow::Field>>
  makeFields(std::vector<std::string> const& columns) {
    std::vector<std::shared_ptr<arrow::Field>> fields;
    fields.reserve(columns.size());
    for(auto const& column : columns) {
      fields.push_back(std::make_shared<arrow::Field>(column, nullptr));
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
