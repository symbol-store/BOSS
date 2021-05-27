#include <catch2/catch.hpp>

#include "Engines/MLIREngine.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Utilities.hpp"
#include "Engines/MLIREngine/Runtime/HashAggregate.hpp"
#include <iostream>
using boss::utilities::operator""_;

TEST_CASE("STORAGE_TEST") {

  SECTION("BULK_LOAD") {
    new_runtime::Relation relation;

    relation.bulk_load({{{"A", "Add"_(50, 60)}, {"B", true}},
                        {{"A", 1}, {"B", 2}},
                        {{"A", "Undefined"_}, {"B", false}},
                        {{"A", "Redefined"_}, {"B", false}},
                        {{"A", "foo"_("x"_)}, {"B", false}},
                        {{"A", "foo"_("y"_)}, {"B", false}},
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

    std::cout << data->ToString() << std::endl;
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

    boss::engines::mlir::Engine engine(database);

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

    boss::engines::mlir::Engine engine(database);

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
    boss::engines::mlir::Engine engine(database);

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
    boss::engines::mlir::Engine engine(database);

    auto result = engine.evaluate(
        "CollectTuples"_(
            "Select"_(
                "Where"_("Eq"_("A"_, 1)),
                "GetRelation"_(std::string("Foo")))
            ));

    auto pointer = std::get<size_t>(result);
    auto* resultRelation = reinterpret_cast<new_runtime::Relation*>(pointer);

    auto firstStruct =
        std::dynamic_pointer_cast<arrow::StructArray>(resultRelation->get()->field(0));
    auto intColumn = std::dynamic_pointer_cast<arrow::Int32Array>(firstStruct->field(0));

    CHECK(intColumn->length() == 1);
  }

  SECTION("Join") {
    new_runtime::Relation leftRelation;
    new_runtime::Relation rightRelation;

    leftRelation.bulk_load({{{"A", 1}}, {{"A", 2}}});

    rightRelation.bulk_load({{{"B", 1}}, {{"B", 4}}});

    new_runtime::Database database;

    database.addRelation("Left", std::move(leftRelation));
    database.addRelation("Right", std::move(rightRelation));

    auto query = "CollectTuples"_(
        "Join"_("On"_("Pair"_("A", "B")), "GetRelation"_("Left"), "GetRelation"_("Right")));

    interpreter::Interpreter i(&database);

    auto result = i.evaluate(query);

    auto pointer = std::get<size_t>(result);
    auto* resultRelation = reinterpret_cast<new_runtime::Relation*>(pointer);

    std::cout << resultRelation->get()->ToString();
  }

  SECTION("GroupBy") {
    new_runtime::Relation relation;

    relation.bulk_load({{{"A", 1}, {"B", 100}},
                        {{"A", 1}, {"B", 100}},
                        {{"A", 2}, {"B", 10}},
                        {{"A", 2}, {"B", 10}}});

    auto query = "GroupBy"_("Fields"_("A"),
                            "Lambda"_("Args"_("Pair"_("currentValue", "Int")),
                                      "Plus"_("Symbol"_("currentValue"), "Symbol"_("B"))),
                            "GetRelation"_("Foo"));

    new_runtime::Database database;

    database.addRelation("Foo", std::move(relation));

    interpreter::Interpreter i(&database);

    auto result = i.evaluate(query);

    auto ptr = std::get<size_t>(result);
    auto aggregate = reinterpret_cast<runtime::aggregate::HashAggregate*>(ptr);

    auto firstVal =  std::get<int>((aggregate->begin())->second);
    auto secondVal = std::get<int>((++aggregate->begin())->second);

    CHECK(firstVal != secondVal);
  }

  SECTION("SelectSymbol") {
    new_runtime::Relation relation;

    relation.bulk_load({
                           {{"A", 1}, {"B", 5}},
                           {{"A", "x"_}, {"B", 3}}
                       });

    new_runtime::Database database;

    database.addRelation("Foo", std::move(relation));
    boss::engines::mlir::Engine engine(database);

    auto resOnlyInts = engine.evaluate(
        "CollectTuples"_(
            "Select"_(
                "Where"_("Eq"_("Symbol"_("A"), 1)),
                "GetRelation"_(std::string("Foo")))
        ));

    auto intsPtr = std::get<size_t>(resOnlyInts);
    auto* onlyIntsRel = reinterpret_cast<new_runtime::Relation*>(intsPtr);

    std::cout << onlyIntsRel->get()->ToString() << std::endl;
  }

  SECTION("AssumingOperator") {
    new_runtime::Relation relation;

    relation.bulk_load({
                           {{"A", 1}, {"B", 5}},
                           {{"A", "x"_}, {"B", 6}},
//                           {{"A", "y"_}, {"B", 7}},
                           {{"A", "x"_}, {"B", 7}}
                       });

    std::cout << relation.get()->ToString() << std::endl;

    new_runtime::Database database;

    database.addRelation("Foo", std::move(relation));
    boss::engines::mlir::Engine engine(database);

    auto result = engine.evaluate(
        "Assuming"_("x"_, 5, "GroupBy"_("Fields"_("A"),
                                          "Lambda"_("Args"_("Pair"_("currentValue", "Int")),
                                                    "Plus"_("Symbol"_("currentValue"), "Symbol"_("B"))),
                                          "GetRelation"_("Foo"))));

    auto ptr = std::get<size_t>(result);
    auto* group = reinterpret_cast<runtime::aggregate::HashAggregate*>(ptr);

    for (auto it = group->begin(); it != group->end(); it++) {
      std::cout << it->second << std::endl;
    }
  }

  SECTION("NextValue") {
    new_runtime::Relation relation;

    relation.bulk_load({
                           {{"A", 1}, {"B", 5}},
                           {{"A", "NextValue"_(1)}, {"B", 6}},
                           {{"A", 2}, {"B", 7}}
                       });

    new_runtime::Database database;

    std::cout << relation.get()->ToString() << std::endl;

    database.addRelation("Foo", std::move(relation));
    boss::engines::mlir::Engine engine(database);

    auto result = engine.evaluate("CollectTuples"_("GetRelation"_("Foo")));

    auto ptr = std::get<size_t>(result);
    auto* resultRel = reinterpret_cast<new_runtime::Relation*>(ptr);

    std::cout << resultRel->get()->ToString() << std::endl;
  }

//  SECTION("Interpolation") {
//    new_runtime::Relation relation;
//
//    relation.bulk_load({
//                           {{"A", 1}, {"B", 5}},
//                           {{"A", "x"_}, {"B", 6}},
//                           {{"A", 2}, {"B", 7}}
//                       });
//
//    new_runtime::Database database;
//    database.addRelation("Foo", std::move(relation));
//    boss::engines::mlir::Engine engine(database);
//
//    auto result = engine.evaluate(
//        "Assuming"_("x"_, "NextValue"_(1), "GroupBy"_("Fields"_("A"),
//                                        "Lambda"_("Args"_("Pair"_("currentValue", "Int")),
//                                                  "Plus"_("Symbol"_("currentValue"), "Symbol"_("B"))),
//                                        "GetRelation"_("Foo"))));
//
//    auto ptr = std::get<size_t>(result);
//    auto* group = reinterpret_cast<runtime::aggregate::HashAggregate*>(ptr);
//
//    for (auto it = group->begin(); it != group->end(); it++) {
//      std::cout << it->second << std::endl;
//    }
//  }

}