#include "../Source/BOSS.hpp"
#include "../Source/Engines/MLIREngine.hpp"
#include "../Source/Engines/MLIREngine/Runtime/Storage.hpp"
#include "../Source/Expression.hpp"
#include "../Source/Utilities.hpp"
#include "ITTNotifySupport.hpp"
#include <benchmark/benchmark.h>
#include <iostream>
using namespace std;

using boss::utilities::operator""_;

// static auto const vtune = VTuneAPIInterface{"BOSS"};

#include "CompileBenchmarks.inc.cpp"
#include "GroupingBenchmarks.inc.cpp"
#include "WolframComparison.inc.cpp"

static void VaryingSymbols(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/VaryingAdd500k-" + std::to_string(state.range(0)) + ".0.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query = "GroupBy"_(
      "Fields"_("B"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("Symbol"_("currentValue"), "A"_)),
      "GetRelation"_("Integers")
  );

  for(auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(VaryingSymbols)->Unit(benchmark::kMillisecond)->Arg(0)->Arg(20)->Arg(40)->Arg(60)->Arg(80)->Arg(100);

static void VaryingNextValueSymbols(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/VaryingNextValue500k-" + std::to_string(state.range(0)) + ".0.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query = "GroupBy"_(
      "Fields"_("B"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("Symbol"_("currentValue"), "A"_)),
      "GetRelation"_("Integers")
  );

  for(auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(VaryingNextValueSymbols)->Unit(benchmark::kMillisecond)->Arg(0)->Arg(20)->Arg(40)->Arg(60)->Arg(80)->Arg(100);

static void VaryingUndefSymbols(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/VaryingUndef500k-" + std::to_string(state.range(0)) + ".0.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query =
      "Assuming"_(
        "undef"_,
        0,
        "GroupBy"_(
            "Fields"_("B"),
            "Lambda"_(
                "Args"_("Pair"_("currentValue", "Int")),
                "Plus"_("Symbol"_("currentValue"), "A"_)),
            "GetRelation"_("Integers")
      )
  );

  for(auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(VaryingUndefSymbols)->Unit(benchmark::kMillisecond)->Arg(0)->Arg(20)->Arg(40)->Arg(60)->Arg(80)->Arg(100);

static void VaryingUndefNextValueSymbols(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/VaryingUndef500k-" + std::to_string(state.range(0)) + ".0.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query =
      "Assuming"_(
          "undef"_,
          "NextValue"_(1, "Int"_),
          "GroupBy"_(
              "Fields"_("B"),
              "Lambda"_(
                  "Args"_("Pair"_("currentValue", "Int")),
                  "Plus"_("Symbol"_("currentValue"), "A"_)),
              "GetRelation"_("Integers")
          )
      );

  for(auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(VaryingUndefNextValueSymbols)->Unit(benchmark::kMillisecond)->Arg(0)->Arg(20)->Arg(40)->Arg(60)->Arg(80)->Arg(100);

static void TPCHQ6(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/lineitem-csv-" + std::to_string(state.range(0)) + ".csv");
  new_runtime::Database database;
  database.addRelation("LINEITEM", std::move(relation));
  boss::engines::mlir::Engine engine(database);

//  auto query =
//      "GroupBy"_(
//        "Fields"_("DummyField"),
//        "Lambda"_(
//            "Args"_("Pair"_("currentValue", "Int")),
//            "Plus"_("Symbol"_("currentValue"), "Mul"_("L_EXTENDEDPRICE"_, "L_DISCOUNT"_))),
//        "Select"_(
//              "Where"_(
//                  "And"_(
//                      "Less"_("L_QUANTITY"_, 24), "Greater"_("L_DISCOUNT"_, 0.05f),
//                      "Greater"_(0.07f, "L_DISCOUNT"_),
//                      "Greater"_(788914800, "L_SHIPDATE"_),
//                      "Greater"_("L_SHIPDATE"_, 757378800)
//                      )
//                  ),
//                "GetRelation"_("LINEITEM")
//              )
//      );
        auto query =
      "GroupBy"_(
        "Fields"_("DummyField"),
        "Lambda"_(
            "Args"_("Pair"_("currentValue", "Int")),
            "Plus"_("Symbol"_("currentValue"), "Mul"_("L_EXTENDEDPRICE"_, "L_DISCOUNT"_))),
          "Select"_(
              "Where"_("Less"_("L_QUANTITY"_, 24)),
              "Select"_(
                  "Where"_("Greater"_("L_DISCOUNT"_, 5)),
                  "Select"_(
                      "Where"_("Greater"_(7, "L_DISCOUNT"_)),
                      "Select"_(
                          "Where"_("Greater"_(788914800, "L_SHIPDATE"_)),
                          "Select"_(
                              "Where"_("Greater"_("L_SHIPDATE"_, 757378800)),
                              "GetRelation"_("LINEITEM")
                              )
                          )
                      )
                  )
              )
      );

  for (auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(TPCHQ6)->Unit(benchmark::kMillisecond)->Arg(1024)->Arg(4096)->Arg(32768)->Arg(262144)->Arg(2097152)->Arg(16777216);

BENCHMARK_MAIN();
