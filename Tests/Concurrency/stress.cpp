// Runs threads × iterations of fixed BOSS expression suites under ThreadSanitizer; `--help`
// lists the options. The concurrency-stress job in .github/workflows/ccpp.yml builds and runs it.

#include "BOSS.hpp"       // boss::evaluate
#include "Expression.hpp" // boss::Expression
#include "ExpressionParser.hpp"
#include "ExpressionUtilities.hpp" // boss::utilities::operator""_

extern "C" {
#include <chibi/sexp.h>
}

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

/* ─── Expression suites ─── */

// "pure": read-only with respect to the singleton BootstrapEngine. Exercises parsing, the
// FFI value conversions, dispatch-table *reads*, GC root management, and serialization.
// Per-thread-context is expected to be TSan-clean on this suite.
std::vector<std::string> const kPureSuite = {
    "Plus(1, 2)",
    "List(1, 2, 3, 4, 5)",
    "Times(Plus(1, 2), Minus(10, 4))",
    "\"a string literal\"",
    "Equals(Symbol, Symbol)",
    "List(\"mixed\", 1, 2.5, True, Symbol)",
    "Plus(1, Plus(2, Plus(3, Plus(4, 5))))", // nested → recursive dispatch + deep GC roots
    "List(\"unicode: \\u00e9\\u4e2d\")",     // exercises preprocessUnicodeEscapes()
};

// "pipeline": bootstrap commands that reconfigure the process-wide engine pipeline, so they
// contend on shared state even with one context per thread. None of them loads an engine.
std::vector<std::string> const kPipelineSuite = {
    "GetDefaultEnginePipeline()",
    "SetDefaultEnginePipeline(\"./libNonexistentEngine.so\")",
    "GetDefaultEnginePipeline()",
    "ResetEngines()",
};

// "mixed": interleaves both. Some evaluations will error once a pipeline is set (the wrapped
// expression tries to dlopen a nonexistent engine and BOSSEvaluate returns an error). Errors
// are fine here — we are hunting races, not checking results.
std::vector<std::string> buildMixedSuite() {
  std::vector<std::string> s;
  for(size_t i = 0; i < kPureSuite.size(); ++i) {
    s.push_back(kPureSuite[i]);
    if(i < kPipelineSuite.size()) {
      s.push_back(kPipelineSuite[i]);
    }
  }
  return s;
}

/* ─── Direct-dispatch expressions ─── */

// Built with the C++ expression API and passed to boss::evaluate() without chibi, whose parse
// and GC work hides the short dispatch window from TSan.
// NOLINTBEGIN(readability-magic-numbers) -- literal operands are the point of the fixtures
// Read-only with respect to the singleton engine.
boss::Expression pureDirectExpr(std::uint64_t i) {
  using boss::utilities::operator""_;
  switch(i % 4) {
  case 0:
    return "Plus"_(1, 2);
  case 1:
    return "List"_(1, 2, 3, 4, 5);
  case 2:
    return "Times"_("Plus"_(1, 2), "Minus"_(10, 4));
  default:
    return "List"_("mixed", 1, 2.5, true);
  }
}

// Reads and reconfigures the singleton engine's pipeline and library cache.
boss::Expression pipelineDirectExpr(std::uint64_t i) {
  using boss::utilities::operator""_;
  switch(i % 4) {
  case 0:
    return "GetDefaultEnginePipeline"_();
  case 1:
    return "SetDefaultEnginePipeline"_("./libNonexistentEngine.so");
  case 2:
    return "GetDefaultEnginePipeline"_();
  default:
    return "ResetEngines"_();
  }
}

// validateConfig has already rejected any suite other than pure, pipeline and mixed. mixed
// alternates the two, like the mixed chibi suite, so dispatch and reconfiguration interleave.
boss::Expression directExpr(std::string const& suite, std::uint64_t i) {
  if(suite == "pipeline") {
    return pipelineDirectExpr(i);
  }
  if(suite == "mixed") {
    return i % 2 == 0 ? pureDirectExpr(i / 2) : pipelineDirectExpr(i / 2);
  }
  return pureDirectExpr(i);
}
// NOLINTEND(readability-magic-numbers)

// Odd multiplier that spreads each thread across the suite instead of having them all walk
// it in step, so concurrent iterations hit different expressions.
constexpr unsigned threadStride = 131U;

/* ─── Configuration ─── */

enum class Mode : std::uint8_t { Shared, PerThread };

struct Config {
  int threads = 8;
  int iters = 1000;
  Mode mode = Mode::PerThread;
  std::string suite = "pure";
  std::string
      enginePath; // optional: dispatch through this engine, so workers race on the library cache
  bool direct = false; // bypass chibi; hammer boss::evaluate() dispatch directly
  bool warmup = true;  // create one context before the workers start
  unsigned seed = 1;
  bool quiet = false;
};

struct Counts {
  std::uint64_t ok = 0;
  std::uint64_t err = 0;
};

std::vector<std::string> const& suiteFor(std::string const& name) {
  static std::vector<std::string> const mixed = buildMixedSuite();
  if(name == "pipeline") {
    return kPipelineSuite;
  }
  if(name == "mixed") {
    return mixed;
  }
  return kPureSuite; // default
}

/* ─── Worker ─── */

// Runs `iters` evaluations against `ctx`/`env`, recording ok/err counts into `out` (a slot
// owned exclusively by this thread — no sharing, so the harness itself is TSan-clean).
void runWorker(sexp ctx, sexp env, std::vector<std::string> const& suite, int threadIdx,
               Config const& cfg, Counts& out) {
  for(int i = 0; i < cfg.iters; ++i) {
    auto const& expr = suite[(cfg.seed + static_cast<unsigned>(threadIdx) * threadStride +
                              static_cast<unsigned>(i)) %
                             suite.size()];
    boss::EvalResult const r = boss::evaluate_expression(ctx, env, expr, /*pretty=*/false);
    if(r.is_error) {
      ++out.err;
    } else {
      ++out.ok;
    }
  }
}

// Direct-dispatch worker: calls boss::evaluate() (BOSS.cpp) directly, no chibi context.
void runDirectWorker(std::string const& suite, int threadIdx, Config const& cfg, Counts& out) {
  for(int i = 0; i < cfg.iters; ++i) {
    std::uint64_t const idx = cfg.seed + static_cast<std::uint64_t>(threadIdx) * threadStride +
                              static_cast<std::uint64_t>(i);
    try {
      boss::Expression const r = boss::evaluate(directExpr(suite, idx));
      (void)r;
      ++out.ok;
    } catch(...) {
      ++out.err;
    }
  }
}

/* ─── Argument parsing ─── */

[[noreturn]] void usageAndExit(char const* prog, int code) {
  std::cerr
      << "Usage: " << prog << " [options]\n"
      << "  --threads N      worker threads (default 8)\n"
      << "  --iters M        iterations per thread (default 1000)\n"
      << "  --mode MODE      shared | per-thread (default per-thread)\n"
      << "  --direct         bypass chibi; hammer boss::evaluate() dispatch directly\n"
      << "                   (best at surfacing the engine-dispatch races; ignores --mode)\n"
      << "  --suite SUITE    pure | pipeline | mixed (default pure)\n"
      << "  --engine PATH    with --suite pure, also exercise dispatch through this engine .so\n"
      << "                   (preloads the pipeline so workers race on the dlopen cache)\n"
      << "  --no-warmup      skip creating and destroying one context before the workers start\n"
      << "                   (first-use setup then happens during the run)\n"
      << "  --seed S         RNG seed for per-thread expression ordering (default 1)\n"
      << "  --quiet          do not print the per-thread error breakdown\n"
      << "  --help           show this help\n";
  std::exit(code);
}

// Rejects configurations that would otherwise run something other than what was asked for.
void validateConfig(Config const& cfg, char const* program) {
  if(cfg.threads < 1 || cfg.iters < 0) {
    std::cerr << "threads must be >= 1 and iters >= 0\n";
    usageAndExit(program, 1);
  }
  if(cfg.suite != "pure" && cfg.suite != "pipeline" && cfg.suite != "mixed") {
    std::cerr << "unknown --suite '" << cfg.suite << "'; expected pure, pipeline or mixed\n";
    usageAndExit(program, 1);
  }
  if(!cfg.enginePath.empty() && cfg.suite != "pure") {
    // Every pipeline expression is a bootstrap command, which BootstrapEngine never wraps in
    // the engine pipeline, and the mixed workers replace and clear the preset pipeline, so only
    // the pure suite keeps dispatching through the engine.
    std::cerr << "--engine needs the pure suite; pipeline never reaches an engine and mixed "
                 "replaces the engine pipeline\n";
    usageAndExit(program, 1);
  }
}

Config parseArgs(int argc, char** argv) {
  Config cfg;
  for(int i = 1; i < argc; ++i) {
    std::string const a = argv[i];
    auto next = [&](char const* name) -> std::string {
      if(i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        usageAndExit(argv[0], 1);
      }
      return argv[++i];
    };
    if(a == "--threads") {
      cfg.threads = std::stoi(next("--threads"));
    } else if(a == "--iters") {
      cfg.iters = std::stoi(next("--iters"));
    } else if(a == "--mode") {
      std::string const m = next("--mode");
      if(m == "shared") {
        cfg.mode = Mode::Shared;
      } else if(m == "per-thread") {
        cfg.mode = Mode::PerThread;
      } else {
        std::cerr << "Unknown mode: " << m << "\n";
        usageAndExit(argv[0], 1);
      }
    } else if(a == "--suite") {
      cfg.suite = next("--suite");
    } else if(a == "--engine") {
      cfg.enginePath = next("--engine");
    } else if(a == "--direct") {
      cfg.direct = true;
    } else if(a == "--no-warmup") {
      cfg.warmup = false;
    } else if(a == "--seed") {
      cfg.seed = static_cast<unsigned>(std::stoul(next("--seed")));
    } else if(a == "--quiet") {
      cfg.quiet = true;
    } else if(a == "--help" || a == "-h") {
      usageAndExit(argv[0], 0);
    } else {
      std::cerr << "Unknown option: " << a << "\n";
      usageAndExit(argv[0], 1);
    }
  }
  validateConfig(cfg, argv[0]);
  return cfg;
}

char const* modeName(Mode m) { return m == Mode::Shared ? "shared" : "per-thread"; }

} // namespace

// The score here is almost all the flat if/else chain that parses argv: a long sequence of
// independent, self-explanatory branches, which the metric counts but a reader does not.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char** argv) try {
  Config const cfg = parseArgs(argc, argv);
  std::vector<std::string> const& suite = suiteFor(cfg.suite);

  std::cout << "boss_concurrency_stress: mode=" << (cfg.direct ? "direct" : modeName(cfg.mode))
            << " suite=" << cfg.suite << " threads=" << cfg.threads << " iters=" << cfg.iters
            << (cfg.enginePath.empty() ? "" : (" engine=" + cfg.enginePath))
            << " warmup=" << (cfg.warmup && !cfg.direct ? "on" : "off") << "\n"
            << std::flush;

  // The warm-up creates and destroys one context before any worker thread exists, so that
  // first-use setup stays out of what TSan reports. --no-warmup leaves that setup in the run.
  if(cfg.warmup && !cfg.direct) {
    sexp warm = boss::initialize_boss_context();
    if(warm != nullptr) {
      sexp_destroy_context(warm);
    }
  }

  std::vector<Counts> counts(static_cast<size_t>(cfg.threads));
  auto const start = std::chrono::steady_clock::now();
  int exitCode = 0;
  std::atomic<bool> contextCreationFailed {false};

  // Optionally preset an engine pipeline, so that dispatches go through a real engine and race
  // on the library cache. The C++ API passes the path as a string, so it needs no escaping.
  if(!cfg.enginePath.empty()) {
    using boss::utilities::operator""_;
    auto const setEnginePipeline = [&cfg] {
      return boss::evaluate("SetDefaultEnginePipeline"_(cfg.enginePath)) == std::string("okay");
    };
    if(!setEnginePipeline()) {
      std::cerr << "SetDefaultEnginePipeline(" << cfg.enginePath << ") was rejected\n";
      return 1;
    }
    // SetDefaultEnginePipeline only records the path, so one dispatch through it checks that the
    // engine loads and evaluates. ResetEngines then unloads it again, so the workers still race
    // on a cold library cache.
    auto const probe = boss::evaluate("Plus"_(1, 2));
    auto const* probeFailure = std::get_if<boss::ComplexExpression>(&probe);
    if(probeFailure != nullptr &&
       probeFailure->getHead() == boss::Symbol("ErrorWhenEvaluatingExpression")) {
      std::cerr << "--engine " << cfg.enginePath << " could not evaluate an expression: " << probe
                << "\n";
      return 1;
    }
    boss::evaluate("ResetEngines"_());
    if(!setEnginePipeline()) {
      std::cerr << "SetDefaultEnginePipeline(" << cfg.enginePath
                << ") was rejected after the check\n";
      return 1;
    }
  }

  if(cfg.direct) {
    // Direct dispatch: no chibi at all.
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(cfg.threads));
    for(int t = 0; t < cfg.threads; ++t) {
      workers.emplace_back([&, t] { runDirectWorker(cfg.suite, t, cfg, counts[t]); });
    }
    for(auto& w : workers) {
      w.join();
    }
  } else {

    // Workers start running as soon as they are created, so without this barrier the first
    // worker could finish before the last worker exists. --no-warmup would then not make the
    // first initialize_boss_context calls overlap, and the Debug tripwire would see no
    // concurrent evaluate_expression calls on the shared context.
    std::atomic<int> workersAtStartingLine {0};
    auto const waitForAllWorkers = [&workersAtStartingLine, &cfg] {
      ++workersAtStartingLine;
      while(workersAtStartingLine.load() < cfg.threads) {
        std::this_thread::yield();
      }
    };
    if(cfg.mode == Mode::Shared) {
      // One context, shared by every thread — the known-unsafe baseline.
      sexp ctx = boss::initialize_boss_context();
      if(ctx == nullptr) {
        std::cerr << "Failed to initialize shared context\n";
        return 1;
      }
      boss::BossContextGuard const guard {ctx};
      sexp env = sexp_context_env(ctx);

      std::vector<std::thread> workers;
      workers.reserve(static_cast<size_t>(cfg.threads));
      for(int t = 0; t < cfg.threads; ++t) {
        workers.emplace_back([&, t] {
          waitForAllWorkers();
          runWorker(ctx, env, suite, t, cfg, counts[t]);
        });
      }
      for(auto& w : workers) {
        w.join();
      }
    } else {
      // One context per thread — the target configuration.
      std::vector<std::thread> workers;
      workers.reserve(static_cast<size_t>(cfg.threads));
      for(int t = 0; t < cfg.threads; ++t) {
        workers.emplace_back([&, t] {
          waitForAllWorkers();
          sexp ctx = boss::initialize_boss_context();
          if(ctx == nullptr) {
            std::cerr << "thread " << t << ": failed to initialize context\n";
            counts[t].err = static_cast<std::uint64_t>(cfg.iters);
            contextCreationFailed = true;
            return;
          }
          boss::BossContextGuard const guard {ctx};
          sexp env = sexp_context_env(ctx);
          runWorker(ctx, env, suite, t, cfg, counts[t]);
        });
      }
      for(auto& w : workers) {
        w.join();
      }
    }
  }

  if(contextCreationFailed) {
    std::cerr << "a worker could not create its chibi context, so this run is not a valid result\n";
    exitCode = 1;
  }

  auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  std::uint64_t totalOk = 0;
  std::uint64_t totalErr = 0;
  for(int t = 0; t < cfg.threads; ++t) {
    totalOk += counts[t].ok;
    totalErr += counts[t].err;
    if(!cfg.quiet) {
      std::cout << "  thread " << t << ": ok=" << counts[t].ok << " err=" << counts[t].err << "\n";
    }
  }
  std::cout << "done: evals=" << (totalOk + totalErr) << " ok=" << totalOk << " err=" << totalErr
            << " elapsed_ms=" << elapsed << "\n"
            << "(data races, if any, are reported by ThreadSanitizer above)\n"
            << std::flush;

  if(cfg.suite == "pure" && totalErr != 0) {
    std::cerr << "the pure suite expects no evaluation errors, so this run is not a valid result\n";
    exitCode = 1;
  }

  return exitCode;
} catch(std::exception const& e) {
  std::cerr << "fatal: " << e.what() << "\n";
  return 1;
}
