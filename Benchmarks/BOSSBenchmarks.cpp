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

#include "WolframComparison.inc.cpp"
#include "GroupingBenchmarks.inc.cpp"
#include "SelectionBenchmarks.inc.cpp"

static void IntegerScanBaseline(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + ".csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate("CollectTuples"_("GetRelation"_(std::string("Integers"))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerScanBaseline)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void IntegerScanWithAdd5PercentBaseline(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "-0.05Add.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate("CollectTuples"_("GetRelation"_(std::string("Integers"))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerScanWithAdd5PercentBaseline)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void IntegerScanWithAdd95PercentBaseline(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "-0.95Add.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate("CollectTuples"_("GetRelation"_(std::string("Integers"))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerScanWithAdd95PercentBaseline)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void ScanWithAssuming5PercentConstantIntSymbol(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "-0.05UnknownSymbol.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate("Assuming"_("unknown"_, 0, "CollectTuples"_("GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(ScanWithAssuming5PercentConstantIntSymbol)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void ScanWithAssuming95PercentConstantIntSymbol(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "-0.95UnknownSymbol.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate("Assuming"_("unknown"_, 0, "CollectTuples"_("GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(ScanWithAssuming95PercentConstantIntSymbol)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void ScanInteger5PercentNextValue(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "-0.05NextValue.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate("Assuming"_("unknown"_, "NextValue"_(1, "Int"_), "CollectTuples"_("GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(ScanInteger5PercentNextValue)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void ScanInteger95PercentNextValue(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "-0.95NextValue.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate( "CollectTuples"_("GetRelation"_(std::string("Integers"))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(ScanInteger95PercentNextValue)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);


static void ScanWithAssuming5PercentPreviousValue(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "-0.05UnknownSymbol.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate( "Assuming"_("unknown"_, "NextValue"_(1, "Int"_), "CollectTuples"_("GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(ScanWithAssuming5PercentPreviousValue)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void ScanWithAssuming95PercentPreviousValue(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "-0.95UnknownSymbol.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate("Assuming"_("unknown"_, "NextValue"_(1, "Int"_), "CollectTuples"_("GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(ScanWithAssuming95PercentPreviousValue)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);


BENCHMARK_MAIN();
