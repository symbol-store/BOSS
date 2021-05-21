#include "Engines/MLIREngine/Interpreter/Interpreter.hpp"
#include "Engines/MLIREngine/Runtime/HashAggregate.hpp"
#include "Utilities.hpp"
#include <catch2/catch.hpp>
#include <iostream>

using boss::utilities::operator""_;

TEST_CASE("InterpreterTest") {
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
}
