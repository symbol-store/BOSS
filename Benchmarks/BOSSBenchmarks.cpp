#include "../Source/BOSS.hpp"
#include "ITTNotifySupport.hpp"
#include <benchmark/benchmark.h>
#include <iostream>
using namespace std;

static auto const vtune = VTuneAPIInterface{"BOSS"};
static void DummyBenchmark(benchmark::State& state) {
  auto dummy = 0;
  vtune.startSampling("DummyBenchmark");
  for(auto _ : state) {                         // NOLINT
    for(auto i = 0U; i < state.range(0); i++) { // NOLINT
      dummy++;
    }
    benchmark::DoNotOptimize(dummy);
  }
  vtune.stopSampling();
}
BENCHMARK(DummyBenchmark)->Range(0, 1024); // NOLINT

BENCHMARK_MAIN(); // NOLINT
