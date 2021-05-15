#pragma once

#include "IterableBuilders.hpp"

#include "../../../Expression.hpp"

#include <arrow/array/array_binary.h>
#include <arrow/array/array_dict.h>
#include <arrow/array/builder_binary.h>
#include <arrow/array/builder_dict.h>
#include <arrow/extension_type.h>

#include <string>

namespace boss::engines::bulk {

class SymbolArray : public arrow::StringArray {
public:
  class SymbolType : public arrow::ExtensionType {
  public:
    explicit SymbolType() : ExtensionType(arrow::utf8()) {}

    std::string extension_name() const override { return "symbol-type"; }

    bool ExtensionEquals(const ExtensionType& other) const override {
      const auto& other_ext = static_cast<const ExtensionType&>(other);
      return other_ext.extension_name() == this->extension_name();
    }

    std::shared_ptr<arrow::Array> MakeArray(std::shared_ptr<arrow::ArrayData> data) const override {
      // temporarly change to the underline type for theconstruction
      // it will be rverted in the SymbolArray constructor
      auto adjustedData = data->Copy();
      adjustedData->type = arrow::utf8();
      return std::make_shared<SymbolArray>(adjustedData);
    }

    arrow::Result<std::shared_ptr<DataType>>
    Deserialize(std::shared_ptr<DataType> /*storage_type*/,
                const std::string& /*serialized*/) const override {
      return std::make_shared<SymbolType>();
    }

    std::string Serialize() const override { return std::string(); }
    // Oh, are these functions required to support arrow format reading/writing?
  };

  explicit SymbolArray(const std::shared_ptr<arrow::ArrayData>& data) : arrow::StringArray(data) {
    // make sure to set back the extension type after the end of call from base array class
    auto adjustedData = data->Copy();
    adjustedData->type = std::make_shared<SymbolType>();
    SetData(adjustedData);
  }
  Symbol Value(size_t index) { return Symbol(std::string(GetView(index))); }
};

class SymbolArrayBuilder : public IterableStringBuilder {
public:
  explicit SymbolArrayBuilder(arrow::MemoryPool* pool = arrow::default_memory_pool())
      : IterableStringBuilder(pool) {
    static bool initialised = false;
    if(!initialised) {
      auto status = arrow::RegisterExtensionType(std::make_shared<SymbolArray::SymbolType>());
      if(!status.ok()) {
        return;
      }
      initialised = true;
    }
  }

  arrow::Status Append(Symbol const& symbol) {
    return IterableStringBuilder::Append(symbol.getName());
  }

  std::shared_ptr<arrow::DataType> type() const override {
    return std::make_shared<SymbolArray::SymbolType>();
  }
};

} // namespace boss::engines::bulk
