#include <catch2/catch.hpp>

#include "Engines/MLIREngine.hpp"
#include "Engines/MLIREngine/Runtime/HashAggregate.hpp"
#include "Engines/MLIREngine/Runtime/Storage.hpp"
#include "Utilities.hpp"
#include <iostream>
#include <fstream>
using boss::utilities::operator""_;

TEST_CASE("MotivatingExample") {

  new_runtime::Relation relation;

  relation.loadFromFile("../DataSets/DiseaseDataset.csv");

  new_runtime::Database database;
  database.addRelation("DiseaseData", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  // clang-format off
  auto result = engine.evaluate(
    "Assuming"_(
      "saturdayVal"_,
      "Div"_("Plus"_("NextValue"_(-1, "Int"_), "NextValue"_(2, "Int"_)), 2),
      "Assuming"_(
        "sundayVal"_,
        "Div"_("Plus"_("NextValue"_(-2, "Int"_), "NextValue"_(1, "Int"_)), 2),
        "CollectTuples"_(
          "Project"_(
            "List"_("DaysSinceStart", "NewCasesToday"),
            "GetRelation"_("DiseaseData")
          )
        )
      )
    )
  );
  // clang-format on

  auto pointer = std::get<size_t>(result);
  auto* resultRelation = reinterpret_cast<new_runtime::Relation*>(pointer);

  std::cout << resultRelation->get()->ToString();

  auto firstStruct = std::dynamic_pointer_cast<arrow::StructArray>(resultRelation->get()->field(0));
  auto resultInts = std::dynamic_pointer_cast<arrow::Int32Array>(firstStruct->field(1));
  auto offsetInts = std::dynamic_pointer_cast<arrow::Int32Array>(firstStruct->field(0));

  std::ofstream outCsv;
  outCsv.open("../DataSets/DiseaseDatasetImputed.csv");

  for (auto i = 0U; i < resultInts->length(); i++) {
    outCsv << offsetInts->Value(i) << "," << resultInts->Value(i) << '\n';
    std::cout << offsetInts->Value(i) << "," << resultInts->Value(i) << '\n';
  }
  outCsv.close();
}
