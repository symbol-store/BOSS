#include <benchmark/benchmark.h>

static void IntegerSelection1(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "Selectivity1.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate(
        "CollectTuples"_(
        "Select"_(
              "Where"_("Eq"_("A"_, 0)),
              "GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerSelection1)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void IntegerSelection5(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "Selectivity5.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate(
        "CollectTuples"_(
            "Select"_(
                "Where"_("Eq"_("A"_, 0)),
                "GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerSelection5)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void IntegerSelection50(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "Selectivity50.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate(
        "CollectTuples"_(
            "Select"_(
                "Where"_("Eq"_("A"_, 0)),
                "GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerSelection50)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void IntegerSelectionSorted50(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "Selectivity50Sorted.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate(
        "CollectTuples"_(
            "Select"_(
                "Where"_("Eq"_("A"_, 0)),
                "GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerSelectionSorted50)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void IntegerSelection95(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + "Selectivity95.csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  for(auto _ : state) {
    auto result = engine.evaluate(
        "CollectTuples"_(
            "Select"_(
                "Where"_("Eq"_("A"_, 0)),
                "GetRelation"_(std::string("Integers")))));
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerSelection95)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);
