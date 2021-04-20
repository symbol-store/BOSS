#include <catch2/catch.hpp>

#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Utilities.hpp"
#include <iostream>
using boss::utilities::operator""_;

TEST_CASE("STORAGE_TEST") {

  SECTION("BULK_LOAD") {
    new_runtime::Relation relation;

    relation.bulk_load({
        {{"A", "Add"_(50, 60)}, {"B", false}},
        {{"A", 1}, {"B", 2}},
        {{"A", "Undefined"_}, {"B", false}},
        {{"A", 42}, {"B", 2}},
        {{"A", "Mul"_(50, 60)}, {"B", false}}
    });

    auto schema = relation.getSchema();

    auto columnAArray = relation.getColumn("A")->chunk(0);
    auto columnADUA = std::dynamic_pointer_cast<arrow::DenseUnionArray>(columnAArray);

    auto intCol = std::dynamic_pointer_cast<arrow::Int32Array>(columnADUA->field(1));

    CHECK(intCol->Value(0) == 1);
    CHECK(intCol->Value(1) == 42);

    auto symbolCol = std::dynamic_pointer_cast<arrow::DictionaryArray>(columnADUA->field(2));
    CHECK(std::dynamic_pointer_cast<arrow::StringArray>(symbolCol->dictionary())
        ->GetString(symbolCol->GetValueIndex(0)) == "Undefined");

    auto exprCol = std::dynamic_pointer_cast<arrow::StructArray>(columnADUA->field(0));
    auto exprSymbols = std::dynamic_pointer_cast<arrow::DictionaryArray>(exprCol->GetFieldByName("head"));
    CHECK(std::dynamic_pointer_cast<arrow::StringArray>(exprSymbols->dictionary())
              ->GetString(exprSymbols->GetValueIndex(0)) == "Add");
    auto exprArg0 = std::dynamic_pointer_cast<arrow::Int32Array>(exprCol->GetFieldByName("arg1"));
    auto exprArg1 = std::dynamic_pointer_cast<arrow::Int32Array>(exprCol->GetFieldByName("arg2"));
    CHECK(exprArg0->Value(0) == 50);
    CHECK(exprArg1->Value(0) == 60);

    auto const* structType = exprCol->struct_type();

    for (auto const& field : structType->fields()) {
      std::cout << field->name() << " " << field->type()->ToString() << std::endl;
    }
  }
}