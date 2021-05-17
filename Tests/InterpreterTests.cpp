#include <catch2/catch.hpp>
#include "Engines/MLIREngine/Interpreter/Interpreter.hpp"
#include "Utilities.hpp"
#include <iostream>

using boss::utilities::operator""_;


TEST_CASE("InterpreterTest") {
    SECTION("Join") {
    new_runtime::Relation leftRelation;
    new_runtime::Relation rightRelation;

    leftRelation.bulk_load({
                               {{"A", 1}},
                               {{"A", 2}}
    });

    rightRelation.bulk_load({
                               {{"B", 1}},
                               {{"B", 4}}
                           });

    new_runtime::Database database;

    database.addRelation("Left", std::move(leftRelation));
    database.addRelation("Right", std::move(rightRelation));

    boss::engines::mlir::Engine engine(database);

    auto query = "CollectTuples"_(
        "Join"_(
            "On"_("Pair"_("A", "B")),
            "GetRelation"_("Left"),
            "GetRelation"_("Right")
        )
    );

    interpreter::Interpreter i(&database);

    auto result = i.evaluate(query);

//    auto result = engine.evaluate(query);
//
    auto pointer = std::get<size_t>(result);
    auto* resultRelation = reinterpret_cast<new_runtime::Relation*>(pointer);

    std::cout << resultRelation->get()->ToString();

  }

}

