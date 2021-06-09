#include <benchmark/benchmark.h>
#include "Engines/MLIREngine/Executors/Compiler.hpp"
#include "Engines/MLIREngine/Executors/Interpreter.hpp"

static void NumberOfPartitionsCompileBenchmark(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/Compile" + std::to_string(state.range(0)) + "Partition.csv");
  new_runtime::Database database;
  database.addRelation("Data", std::move(relation));

  boss::engines::mlir::compiler::Compiler compiler(&database);

  auto query = "GroupBy"_(
      "Fields"_("A"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("currentValue"_, "B"_)),
      "Select"_(
          "Where"_("Eq"_("Z"_, 0)),
          "GetRelation"_("Data"))
  );

  for(auto _ : state) {
    auto result = compiler.evaluate(query, true);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(NumberOfPartitionsCompileBenchmark)->Unit(benchmark::kMillisecond)->Arg(1)->Arg(5)->Arg(50)->Arg(100);

static void ProjectionCompileBenchmark(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/Compile1Partition.csv");
  new_runtime::Database database;
  database.addRelation("Data", std::move(relation));

  boss::engines::mlir::compiler::Compiler compiler(&database);

  auto innerQuery = "Select"_(
      "Where"_("Eq"_("Z"_, 0)),
      "GetRelation"_("Data"));

  for (auto i = 0; i < state.range(0); i++) {
    innerQuery = "Project"_("List"_("A", "B"), innerQuery);
  }

  auto query = "GroupBy"_(
      "Fields"_("A"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("currentValue"_, "B"_)),
          innerQuery
  );

  for(auto _ : state) {
    auto result = compiler.evaluate(query, true);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(ProjectionCompileBenchmark)->Unit(benchmark::kMillisecond)->Arg(1)->Arg(5)->Arg(25)->Arg(50)->Arg(75)->Arg(100);

static void SelectionCompileBenchmark(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/Compile1Partition.csv");
  new_runtime::Database database;
  database.addRelation("Data", std::move(relation));

  boss::engines::mlir::compiler::Compiler compiler(&database);

  auto innerQuery = "Select"_(
      "Where"_("Eq"_("Z"_, 0)),
      "GetRelation"_("Data"));

  for (auto i = 0; i < state.range(0); i++) {
    innerQuery = "Select"_("Where"_("Eq"_("B"_, 0)), innerQuery);
  }

  auto query = "GroupBy"_(
      "Fields"_("A"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("currentValue"_, "B"_)),
      innerQuery
  );

  for(auto _ : state) {
    auto result = compiler.evaluate(query, true);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(SelectionCompileBenchmark)->Unit(benchmark::kMillisecond)->Arg(1)->Arg(5)->Arg(25)->Arg(50)->Arg(75)->Arg(100);

static void AssumingCompileBenchmark(benchmark::State& state) {
  new_runtime::Relation relation;
  relation.loadFromFile("../DataSets/Compile" + std::to_string(state.range(0)) + "Partition.csv");
  new_runtime::Database database;
  database.addRelation("Data", std::move(relation));

  interpreter::Interpreter interpreter(&database);

  auto query = "GroupBy"_(
      "Fields"_("A"),
      "Lambda"_(
          "Args"_("Pair"_("currentValue", "Int")),
          "Plus"_("currentValue"_, "B"_)),
      "GetRelation"_("Data")
  );

  for (auto i = 0; i < state.range(0); i++) {
    query = "Assuming"_(boss::Symbol("sym" + std::to_string(i)), "Add"_(1, 2), query);
  }

//  std::cout << query << std::endl;

    for(auto _ : state) {
    auto result = interpreter.evaluate(query, true);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(AssumingCompileBenchmark)->Unit(benchmark::kMillisecond)->Arg(1)->Arg(5)->Arg(50)->Arg(100);