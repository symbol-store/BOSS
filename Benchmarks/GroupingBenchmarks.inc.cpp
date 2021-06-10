#include <benchmark/benchmark.h>

static void IntegerGrouping(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/IntegerDataset" + std::to_string(state.range(0)) + ".csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query = "GroupBy"_(
      "Fields"_("B"),
      "Lambda"_(
        "Args"_("Pair"_("currentValue", "Int")),
        "Plus"_("Symbol"_("currentValue"), 1)),
        "GetRelation"_("Integers")
        );

  for(auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(IntegerGrouping)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(100000)->Arg(500000)->Arg(1000000)->Arg(5000000);
