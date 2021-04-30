#include <catch2/catch.hpp>

#include "Engines/MLIREngine.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Utilities.hpp"
#include <iostream>
using boss::utilities::operator""_;

TEST_CASE("STORAGE_TEST") {

  SECTION("BULK_LOAD") {
    new_runtime::Relation relation;

    relation.bulk_load({
        {{"A", "Add"_(50, 60)}, {"B", true}},
        {{"A", 1}, {"B", 2}},
        {{"A", "Undefined"_}, {"B", false}},
        {{"A", "Redefined"_}, {"B", false}},
        {{"A", 42}, {"B", 2}},
        {{"A", "Mul"_(50, 60)}, {"B", false}},
        {{"A", "Mul"_(60, 80)}, {"B", true}}
    });

    new_runtime::Database database;
    database.addRelation("Foo", std::move(relation));

    auto rel = database.getRelation("Foo");

    auto data = std::dynamic_pointer_cast<arrow::DenseUnionArray>(rel.get());

    auto addAndBoolField = std::dynamic_pointer_cast<arrow::StructArray>(data->field(0));
    auto firstBool = std::dynamic_pointer_cast<arrow::BooleanArray>(addAndBoolField->GetFieldByName("B"));
    CHECK(firstBool->length() == 1);
    CHECK(firstBool->Value(0) == true);

    for (auto const& field : data->union_type()->fields()) {
      std::cout << field->type()->ToString() << std::endl;
    }
  }

  SECTION("Scan and Aggregate") {
    new_runtime::Relation relation;

    relation.bulk_load({
        {{"A", 5}},
        {{"A", 6}},
    });

    new_runtime::Database database;
    database.addRelation("Foo", std::move(relation));

    boss::engines::mlir::Engine engine(std::move(database));

    engine.evaluate("CollectTuples"_("GetRelation"_(std::string("Foo"))));
  }

}