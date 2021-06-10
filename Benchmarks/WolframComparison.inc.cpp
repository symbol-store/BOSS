#include <benchmark/benchmark.h>

static void WolframQuery1(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/ComparisonBenchmark" + std::to_string(state.range(0)) + ".csv");
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
BENCHMARK(WolframQuery1)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);


static void WolframQuery2(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/ComparisonBenchmark" + std::to_string(state.range(0)) + ".csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query = "GroupBy"_(
      "Fields"_("B"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("Symbol"_("currentValue"), "A"_)),
                "Select"_(
                  "Where"_("Less"_("A"_, 0)),
                  "Project"_(
                    "List"_("A", "B"),
                    "GetRelation"_("Integers")
                  )
                )

  );

  for(auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(WolframQuery2)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void WolframQuery3(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/ComparisonBenchmarkQ3-" + std::to_string(state.range(0)) + ".csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query = "GroupBy"_(
      "Fields"_("B"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("Symbol"_("currentValue"), "A"_)),
      "Select"_(
          "Where"_("Less"_("A"_, 0)),
          "Project"_(
              "List"_("A", "B"),
              "GetRelation"_("Integers")
          )
      )
  );

  auto assumingQueryX = "Assuming"_("x"_, "Plus"_(1, 2), query);
  auto assumingQueryY = "Assuming"_("y"_, 0, assumingQueryX);

  for(auto _ : state) {
    auto result = engine.evaluate(assumingQueryY);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(WolframQuery3)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);

static void WolframQueryImpactOfGroup(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/ComparisonBenchmark" + std::to_string(state.range(0)) + ".csv");
  new_runtime::Database database;
  database.addRelation("Integers", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query = "GroupBy"_(
      "Fields"_("B"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("Symbol"_("currentValue"), "A"_)),
      "Select"_(
          "Where"_("Eq"_("A"_, 10000)),
          "Project"_(
              "List"_("A", "B"),
              "GetRelation"_("Integers")
          )
      )
  );

  for(auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(WolframQueryImpactOfGroup)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500000);

static void WolframQuery4(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/StringDataset" + std::to_string(state.range(0)) + ".csv");
  new_runtime::Database database;
  database.addRelation("Strings", std::move(relation));
  boss::engines::mlir::Engine engine(database);

  auto query = "GroupBy"_(
      "Fields"_("C"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("Symbol"_("currentValue"), "A"_)),
      "Select"_(
          "Where"_("Less"_("B"_, 0)),
          "GetRelation"_("Strings")
      )

  );

  for(auto _ : state) {
    auto result = engine.evaluate(query);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(WolframQuery4)->Unit(benchmark::kMillisecond)->Arg(5)->Arg(50)->Arg(500)->Arg(5000)->Arg(50000)->Arg(500000);