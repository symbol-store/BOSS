#include <catch2/catch.hpp>

#include "Engines/MLIREngine.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Utilities.hpp"
#include <iostream>
using boss::utilities::operator""_;

TEST_CASE("STORAGE_TEST") {

  SECTION("BULK_LOAD") {
    new_runtime::Relation relation;

    relation.bulk_load({{{"A", "Add"_(50, 60)}, {"B", true}},
                        {{"A", 1}, {"B", 2}},
                        {{"A", "Undefined"_}, {"B", false}},
                        {{"A", "Redefined"_}, {"B", false}},
                        {{"A", 42}, {"B", 2}},
                        {{"A", "Mul"_(50, 60)}, {"B", false}},
                        {{"A", "Mul"_(60, 80)}, {"B", true}}});

    new_runtime::Database database;
    database.addRelation("Foo", std::move(relation));

    auto rel = database.getRelation("Foo");

    auto data = std::dynamic_pointer_cast<arrow::DenseUnionArray>(rel.get());

    auto addAndBoolField = std::dynamic_pointer_cast<arrow::StructArray>(data->field(0));
    auto firstBool =
        std::dynamic_pointer_cast<arrow::BooleanArray>(addAndBoolField->GetFieldByName("B"));
    CHECK(firstBool->length() == 1);
    CHECK(firstBool->Value(0) == true);

    for(auto const& field : data->union_type()->fields()) {
      std::cout << field->type()->ToString() << std::endl;
    }
  }

  SECTION("Scan and Aggregate") {
    new_runtime::Relation relation;

    relation.bulk_load({{{"A", 5}, {"B", 1}},
                        {{"A", 6}, {"B", 1}},
                        {{"A", 6}, {"B", 1}},
                        {{"A", 3.2F}, {"B", 1}},
                        {{"A", 3.2F}, {"B", 3.3F}}});

    new_runtime::Database database;

    std::cout << relation.get()->ToString() << std::endl;

    database.addRelation("Foo", std::move(relation));

    boss::engines::mlir::Engine engine(std::move(database));

    auto result = engine.evaluate("CollectTuples"_("GetRelation"_(std::string("Foo"))));
    auto pointer = std::get<size_t>(result);
    auto resultRelation = reinterpret_cast<new_runtime::Relation*>(pointer);

    auto firstStruct =
        std::dynamic_pointer_cast<arrow::StructArray>(resultRelation->get()->field(0));
    auto intColumn = std::dynamic_pointer_cast<arrow::Int32Array>(firstStruct->field(0));
    auto secondStruct =
        std::dynamic_pointer_cast<arrow::StructArray>(resultRelation->get()->field(1));
    auto floatColumn = std::dynamic_pointer_cast<arrow::FloatArray>(secondStruct->field(0));

    std::cout << resultRelation->get()->ToString() << std::endl;

    CHECK(intColumn->Value(0) == 5);
    CHECK(intColumn->Value(1) == 6);
    CHECK(floatColumn->Value(0) == 3.2F);
  }

  SECTION("Simple Operation Dispatch") {
    new_runtime::Relation relation;

    relation.bulk_load({
        {{"A", "Plus"_("Plus"_(2, 3), 5)}, {"B", 1}},
    });

    new_runtime::Database database;

    std::cout << relation.get()->ToString() << std::endl;
    std::cout << "Here" << std::endl;

    database.addRelation("Foo", std::move(relation));

    boss::engines::mlir::Engine engine(std::move(database));

    auto result = engine.evaluate("CollectTuples"_("GetRelation"_(std::string("Foo"))));
    auto pointer = std::get<size_t>(result);
    auto resultRelation = reinterpret_cast<new_runtime::Relation*>(pointer);

    auto firstStruct =
        std::dynamic_pointer_cast<arrow::StructArray>(resultRelation->get()->field(0));
    auto intColumn = std::dynamic_pointer_cast<arrow::Int32Array>(firstStruct->field(0));

    std::cout << resultRelation->get()->ToString() << std::endl;

    CHECK(intColumn->Value(0) == 10);
  }

  SECTION("Projection") {
    new_runtime::Relation relation;

    relation.bulk_load({
        {{"A", 1}, {"B", 1}},
        {{"A", 1}, {"B", 1.1f}}
    });

    new_runtime::Database database;

    database.addRelation("Foo", std::move(relation));
    boss::engines::mlir::Engine engine(std::move(database));

    auto result = engine.evaluate(
        "CollectTuples"_("Project"_("List"_("B"), "GetRelation"_(std::string("Foo")))));
    auto pointer = std::get<size_t>(result);
    auto* resultRelation = reinterpret_cast<new_runtime::Relation*>(pointer);

    CHECK(resultRelation->get()->field(0)->num_fields() == 1);
  }

  SECTION("Selection") {
    new_runtime::Relation relation;

    relation.bulk_load({
                           {{"A", 1}, {"B", 5}},
                           {{"A", 2}, {"B", 5}},
                           {{"A", 1}, {"B", 1.1f}},
                           {{"A", 2}, {"B", 3.1f}}
                       });

    new_runtime::Database database;

    database.addRelation("Foo", std::move(relation));
    boss::engines::mlir::Engine engine(std::move(database));

    auto result = engine.evaluate(
        "CollectTuples"_(
            "Select"_(
                "Where"_("Eq"_("Symbol"_("A"), 1)),
                "GetRelation"_(std::string("Foo")))
            ));

    auto pointer = std::get<size_t>(result);
    auto* resultRelation = reinterpret_cast<new_runtime::Relation*>(pointer);

    auto firstStruct =
        std::dynamic_pointer_cast<arrow::StructArray>(resultRelation->get()->field(0));
    auto intColumn = std::dynamic_pointer_cast<arrow::Int32Array>(firstStruct->field(0));

    CHECK(intColumn->length() == 1);
  }
}