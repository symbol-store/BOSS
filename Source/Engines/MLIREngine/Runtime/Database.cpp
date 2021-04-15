#include "Database.hpp"
#include "Engines/MLIREngine/Types/TypeConversions.hpp"
#include "Engines/MLIREngine/Types/Types.hpp"
#include "Utilities.hpp"
#include <algorithm>
#include <sstream>

namespace runtime {

// temporary encoding function. This will need to be improved
std::string encodeExpression(boss::Expression const& e) {
  std::string returnValue;
  std::visit(boss::utilities::overload(
                 [&](boss::ComplexExpression const& e) {
                   std::stringstream str;

                   str << "ComplexExpression{" << e.getHead() << ",";
                   str << "numArgs=" << e.getArguments().size();

                   for(const auto& arg : e.getArguments()) {
                     str << "," << encodeExpression(arg);
                   }
                   str << "}";
                   returnValue = str.str();
                 },
                 [&](boss::Symbol const& e) { returnValue = "Symbol{" + e.getName() + "}"; },
                 [&](bool e) { returnValue = e; }, [&](int e) { returnValue = e; }, [&](size_t e) { returnValue = e; },
                 [&](float e) { returnValue = e; },
                 [&](std::string const& e) { returnValue = "string{" + e + "}"; }),
             e);
  return returnValue;
}

bool expressionIsSymbolic(boss::Expression const& e) {
  return std::holds_alternative<boss::Symbol>(e) ||
         std::holds_alternative<boss::ComplexExpression>(e);
}

void appendToBuilder(boss::Expression const& e, arrow::ArrayBuilder* builder) {
  std::visit(boss::utilities::overload(
                 [&](boss::ComplexExpression e) {
                   dynamic_cast<arrow::BinaryBuilder*>(builder)->Append(encodeExpression(e));
                 },
                 [&](boss::Symbol e) {
                   dynamic_cast<arrow::BinaryBuilder*>(builder)->Append(encodeExpression(e));
                 },
                 [&](bool e) { dynamic_cast<arrow::BooleanBuilder*>(builder)->Append(e); },
                 [&](int e) { dynamic_cast<arrow::Int32Builder*>(builder)->Append(e); },
                 [&](size_t e) { dynamic_cast<arrow::Int64Builder*>(builder)->Append(e); },
                 [&](float e) { dynamic_cast<arrow::FloatBuilder*>(builder)->Append(e); },
                 [&](std::string e) { dynamic_cast<arrow::StringBuilder*>(builder)->Append(e); }),
             e);
}

void Table::bulk_load(std::shared_ptr<arrow::Schema> schema,
                      std::vector<std::map<std::string, boss::Expression>> tuples) {

  std::map<std::string, std::unique_ptr<arrow::ArrayBuilder>> builders;

  // Create the appropriate builder for each field
  for(auto const& field : schema->fields()) {
    // create data field
    switch(field->type()->id()) {
    case arrow::Type::type::INT32:
      builders[field->name()] = std::make_unique<arrow::Int32Builder>();
      break;
    case arrow::Type::type::BOOL:
      builders[field->name()] = std::make_unique<arrow::BooleanBuilder>();
      break;
    case arrow::Type::type::BINARY:
      builders[field->name()] = std::make_unique<arrow::BinaryBuilder>();
      break;
    case arrow::Type::type::STRING:
      builders[field->name()] = std::make_unique<arrow::StringBuilder>();
      break;
    case arrow::Type::type::FLOAT:
      builders[field->name()] = std::make_unique<arrow::FloatBuilder>();
      break;
    }
    // create symbols field
    builders[field->name() + "_symbols"] = std::make_unique<arrow::BinaryBuilder>();
  }

  // Insert data
  for(auto const& tuple : tuples) {
    for(auto const& field : tuple) {
      std::string dataColumnName;
      std::string nullColumnName;
      if(expressionIsSymbolic(field.second)) {
        dataColumnName = field.first + "_symbols";
        nullColumnName = field.first;
      } else {
        dataColumnName = field.first;
        nullColumnName = field.first + "_symbols";
      }
      // Append actual value to correct column
      appendToBuilder(field.second, builders[dataColumnName].get());
      // Append null to other column
      builders[nullColumnName]->AppendNull();
    }
  }

  std::vector<std::shared_ptr<arrow::Array>> columns;

  // Build table
  for(auto const& field : schema->fields()) {
    columns.push_back(builders[field->name()]->Finish().ValueOrDie());
  }

  data = arrow::Table::Make(schema, columns);
}

std::shared_ptr<arrow::ChunkedArray> Table::getColumnDataPtr(std::string name, bool symbolic) {
  if(symbolic) {
    return data->GetColumnByName(name + "_symbols");
  }
  return data->GetColumnByName(name);
}

std::map<std::string, arrow::ArrayBuilder*>* getBuildersForSchema(arrow::Schema* schema) {
  auto* resultMap = new std::map<std::string, arrow::ArrayBuilder*>;

  for(auto const& field : schema->fields()) {
    // TODO could I use unique_ptr?
    switch(field->type()->id()) {
    case arrow::Type::type::INT32:
      resultMap->operator[](field->name()) = new arrow::Int32Builder();
      break;
    case arrow::Type::type::BOOL:
      resultMap->operator[](field->name()) = new arrow::BooleanBuilder();
      break;
    case arrow::Type::type::BINARY:
      resultMap->operator[](field->name()) = new arrow::BinaryBuilder();
      break;
    case arrow::Type::type::STRING:
      resultMap->operator[](field->name()) = new arrow::StringBuilder();
      break;
    case arrow::Type::type::FLOAT:
      resultMap->operator[](field->name()) = new arrow::FloatBuilder();
      break;
    }
  }
  return resultMap;
}

Table* constructTable(arrow::Schema* schema, std::map<std::string, arrow::ArrayBuilder*>* builders) {
  // TODO
  std::vector<std::shared_ptr<arrow::Array>> columns;

  for(auto const& field : schema->fields()) {
    columns.push_back(builders->operator[](field->name())->Finish().ValueOrDie());
  }

  auto sharedSchema = std::shared_ptr<arrow::Schema>(std::shared_ptr<arrow::Schema>(), schema);

  auto arrowTable = arrow::Table::Make(sharedSchema, columns);

  return new Table(Table::fromArrowTable(arrowTable));
}

void addToRelation_Int(arrow::ArrayBuilder* builder, int value) {
  auto status = dynamic_cast<arrow::Int32Builder*>(builder)->Append(value);
}

void addToRelation_Bool(arrow::ArrayBuilder* builder, bool value) {
  auto status = dynamic_cast<arrow::BooleanBuilder*>(builder)->Append(value);
}

} // namespace runtime