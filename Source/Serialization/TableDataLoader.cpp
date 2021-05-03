#include "TableDataLoader.hpp"

#include <arrow/array/array_base.h>
#include <arrow/csv/api.h>
#include <arrow/io/api.h>
#include <arrow/scalar.h>
#include <arrow/visitor.h>
#include <arrow/visitor_inline.h>

#include <chrono>
#include <iostream>

namespace boss::serialization {

bool constexpr VERBOSE_LOADING = false;

struct ConvertToExpressionArgument : public arrow::ScalarVisitor {
  explicit ConvertToExpressionArgument(ExpressionArguments& argList) : m_argList(argList) {}
  template <typename ScalarType> arrow::Status Visit(ScalarType const& scalar) {
    if constexpr(std::is_base_of_v<arrow::internal::PrimitiveScalarBase, ScalarType>) {
      if constexpr(std::is_same_v<bool, typename ScalarType::ValueType>) {
        m_argList.emplace_back(Expression(static_cast<bool>(scalar.value)));
        return arrow::Status::OK();
      } else if constexpr(std::is_integral_v<typename ScalarType::ValueType>) {
        m_argList.emplace_back(Expression(static_cast<int>(scalar.value)));
        return arrow::Status::OK();
      } else if constexpr(std::is_floating_point_v<typename ScalarType::ValueType>) {
        m_argList.emplace_back(Expression(static_cast<float>(scalar.value)));
        return arrow::Status::OK();
      }
    } else if constexpr(std::is_base_of_v<arrow::BaseBinaryScalar, ScalarType>) {
      m_argList.emplace_back(scalar.ToString());
      return arrow::Status::OK();
    }

    return arrow::Status::NotImplemented("Scalar visitor for type not implemented ",
                                         scalar.type->ToString());
  }
  ExpressionArguments& m_argList;
};

/*static*/ bool TableDataLoader::loadInternal(std::string const& filepath, Symbol const& table,
                                              char separator, bool hasHeader,
                                              std::vector<std::string> const& columnNames,
                                              std::function<void(Expression const&)>&& evaluate) {
  arrow::MemoryPool* pool = arrow::default_memory_pool();

  auto maybeFileInput = arrow::io::ReadableFile::Open(filepath, pool);
  if(!maybeFileInput.ok()) {
    return false;
  }
  auto fileInput = *maybeFileInput;

  auto readOptions = arrow::csv::ReadOptions::Defaults();
  if(!hasHeader) {
    readOptions.column_names = columnNames;
  }

  auto parseOptions = arrow::csv::ParseOptions::Defaults();
  parseOptions.delimiter = separator;

  auto convertOptions = arrow::csv::ConvertOptions::Defaults();
  convertOptions.include_columns = columnNames;
  convertOptions.include_missing_columns = true;

  auto maybeReader =
      arrow::csv::StreamingReader::Make(pool, fileInput, readOptions, parseOptions, convertOptions);
  if(!maybeReader.ok()) {
    return false;
  }
  auto reader = *maybeReader;

  std::shared_ptr<arrow::RecordBatch> batch;
  while(reader->ReadNext(&batch).ok() && batch) {
    auto numRows = batch->num_rows();
    auto const& columns = batch->columns();
    for(size_t index = 0; index < numRows; ++index) {
      ExpressionArguments insertRowArguments;
      insertRowArguments.emplace_back(table);
      ConvertToExpressionArgument visitor(insertRowArguments);

      for(auto const& column : columns) {
        auto const& scalarResult = column->GetScalar(index);
        if(scalarResult.ok()) {
          auto const& scalar = *scalarResult.ValueOrDie();
          if(arrow::VisitScalarInline(scalar, &visitor).ok()) {
            continue;
          }
        }

        // fallback - add as a missing value
        // TODO: get from schema what to do for missing data
        insertRowArguments.emplace_back("Missing"_);
      }

      evaluate(ComplexExpression("InsertInto"_, insertRowArguments));

      if constexpr(VERBOSE_LOADING) {
        int constexpr interval = 100;
        if(index % interval == 0) {
          static auto debugStart = std::chrono::high_resolution_clock::now();
          auto debugEnd = std::chrono::high_resolution_clock::now();
          std::chrono::duration<float> elapsed = debugEnd - debugStart;
          auto speed = static_cast<int>(static_cast<float>(interval) / elapsed.count());
          debugStart = debugEnd;
          if(index > 0) {
            std::cerr << " [speed:" << speed << "/s] insert";
            std::cerr << index << "/" << numRows << std::endl;
          }
        }
      }
    }
  }

  return true;
}

} // namespace boss::serialization
