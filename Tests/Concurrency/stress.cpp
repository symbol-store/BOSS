// BOSS core concurrency stress harness (Phase 2 of the thread-safety hardening plan).
//
// Fires N worker threads × M iterations of a fixed expression suite at BOSS core,
// in one of three configurations:
//
//   --mode shared      all threads share ONE chibi context (sexp ctx).
//                      Known-unsafe baseline; the goal is to confirm TSan catches it.
//                      Expect a TSan report (and possibly a crash) — that is success.
//
//   --mode per-thread  each thread builds its own context via initialize_boss_context().
//                      This is the configuration we want clean. With the `pure` suite it
//                      is TSan-clean; the `pipeline` suite hits the *shared* singleton
//                      BootstrapEngine (BOSS.cpp:32) but the chibi parse/GC overhead buries
//                      the race window, so it rarely surfaces here — use --direct for that.
//
//   --direct           bypass chibi; call boss::evaluate() dispatch directly (ignores --mode).
//                      Maximizes the dispatch race window: --direct --suite pipeline reliably
//                      surfaces the singleton-engine defaultEngine race. Race-free since
//                      Phase 4 (engineStateMutex); this is now the regression gate for it.
//
// The suites round-trip purely through core (parse → expr_to_sexp →
// convert-to-boss-expression → BOSSEvaluate dispatch → convert-from → serialize). With no
// default engine pipeline configured, BOSSEvaluate returns expressions unevaluated for
// unknown heads, so no engine shared object is loaded or required: the harness is
// engine-independent by construction (the optional --engine hook is the one exception).
//
// Build with ThreadSanitizer (the boss_concurrency_stress CMake target does this):
//   cmake --build <dir> --target boss_concurrency_stress
//   TSAN_OPTIONS="halt_on_error=1" ./boss_concurrency_stress --mode per-thread --suite pure
//
// See docs/threading-audit.md (the inventory this exercises) and
// Tests/Concurrency/known-races.md (the triage log).

#include "ExpressionParser.hpp"

extern "C" {
#include <chibi/eval.h>
#include <chibi/sexp.h>
}

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

// "pipeline": mutates the singleton's defaultEngine vector (audit item G2) via the bootstrap
// operators. Because the engine is a process singleton, these contend on shared state even in
// per-thread-context mode — the race the Phase 4 engineStateMutex fixes. Best probed with
// --direct (see directExpr). None of these trigger a dlopen (all are bootstrap commands).
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

// Built with the C++ expression API and fed straight to boss::evaluate(), bypassing chibi.
// The chibi path spends almost all of its time in parse/GC, so the brief shared-state access
// inside dispatch rarely overlaps across threads and TSan struggles to catch the dispatch
// races (e.g. the defaultEngine race). Direct mode removes that overhead, so dispatch — the
// plan's prime suspect — dominates each iteration and the race window is wide.
boss::Expression directExpr(std::string const& suite, std::uint64_t i) {
  using boss::utilities::operator""_;
  if(suite == "pipeline") {
    switch(i % 4) {
    case 0:
      return "GetDefaultEnginePipeline"_();
    case 1:
      return "SetDefaultEnginePipeline"_(std::string("./libNonexistentEngine.so"));
    case 2:
      return "GetDefaultEnginePipeline"_();
    default:
      return "ResetEngines"_();
    }
  }
  // pure (and fallback): read-only with respect to the singleton engine.
  switch(i % 4) {
  case 0:
    return "Plus"_(1, 2);
  case 1:
    return "List"_(1, 2, 3, 4, 5);
  case 2:
    return "Times"_("Plus"_(1, 2), "Minus"_(10, 4));
  default:
    return "List"_(std::string("mixed"), 1, 2.5, true);
  }
}

/* ─── Configuration ─── */

enum class Mode { Shared, PerThread };

struct Config {
  int threads = 8;
  int iters = 1000;
  Mode mode = Mode::PerThread;
  std::string suite = "pure";
  std::string enginePath; // optional: exercises the dlopen registry race (audit item G3)
  bool direct = false;    // bypass chibi; hammer boss::evaluate() dispatch directly
  bool warmup = true;     // pre-init chibi globals to avoid the sexp_initialized_p race
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
    auto const& expr =
        suite[(cfg.seed + static_cast<unsigned>(threadIdx) * 131U + static_cast<unsigned>(i)) %
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
    std::uint64_t const idx =
        cfg.seed + static_cast<std::uint64_t>(threadIdx) * 131U + static_cast<std::uint64_t>(i);
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
  std::cerr << "Usage: " << prog << " [options]\n"
            << "  --threads N      worker threads (default 8)\n"
            << "  --iters M        iterations per thread (default 1000)\n"
            << "  --mode MODE      shared | per-thread (default per-thread)\n"
            << "  --direct         bypass chibi; hammer boss::evaluate() dispatch directly\n"
            << "                   (best at surfacing the engine-dispatch races; ignores --mode)\n"
            << "  --suite SUITE    pure | pipeline | mixed (default pure)\n"
            << "  --engine PATH    also exercise dispatch through this engine .so\n"
            << "                   (preloads the pipeline so workers race on the dlopen cache)\n"
            << "  --no-warmup      skip the chibi one-time-init warm-up (exposes the\n"
            << "                   sexp_initialized_p init race in per-thread mode)\n"
            << "  --seed S         RNG seed for per-thread expression ordering (default 1)\n"
            << "  --quiet          do not print the per-thread error breakdown\n"
            << "  --help           show this help\n";
  std::exit(code);
}

Config parseArgs(int argc, char** argv) {
  Config cfg;
  for(int i = 1; i < argc; ++i) {
    std::string a = argv[i];
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
      std::string m = next("--mode");
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
  if(cfg.threads < 1 || cfg.iters < 0) {
    std::cerr << "threads must be >= 1 and iters >= 0\n";
    usageAndExit(argv[0], 1);
  }
  return cfg;
}

char const* modeName(Mode m) { return m == Mode::Shared ? "shared" : "per-thread"; }

} // namespace

int main(int argc, char** argv) try {
  Config const cfg = parseArgs(argc, argv);
  std::vector<std::string> const& suite = suiteFor(cfg.suite);

  std::cout << "boss_concurrency_stress: mode=" << (cfg.direct ? "direct" : modeName(cfg.mode))
            << " suite=" << cfg.suite << " threads=" << cfg.threads << " iters=" << cfg.iters
            << (cfg.enginePath.empty() ? "" : (" engine=" + cfg.enginePath))
            << " warmup=" << (cfg.warmup && !cfg.direct ? "on" : "off") << "\n"
            << std::flush;

  // One-time chibi global init (the sexp_initialized_p guard, audit §3, plus any other lazy
  // process-global setup reachable through context creation) happens on the first context.
  // Building and destroying one full context here, before any worker thread exists, primes
  // all of it so that what TSan reports during the run is the steady-state hazard, not
  // start-up noise. --no-warmup deliberately leaves it in to demonstrate that race.
  if(cfg.warmup && !cfg.direct) {
    sexp const warm = boss::initialize_boss_context();
    if(warm != nullptr) {
      sexp_destroy_context(warm);
    }
  }

  std::vector<Counts> counts(static_cast<size_t>(cfg.threads));
  sexp setupCtx = nullptr;
  auto const start = std::chrono::steady_clock::now();
  int exitCode = 0;

  if(cfg.direct) {
    // Direct dispatch: no chibi at all. Optionally preset the pipeline (audit item G3).
    if(!cfg.enginePath.empty()) {
      using boss::utilities::operator""_;
      boss::evaluate("SetDefaultEnginePipeline"_(cfg.enginePath));
    }
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(cfg.threads));
    for(int t = 0; t < cfg.threads; ++t) {
      workers.emplace_back([&, t] { runDirectWorker(cfg.suite, t, cfg, counts[t]); });
    }
    for(auto& w : workers) {
      w.join();
    }
  } else {
    // Optionally preload an engine pipeline so the `pure`/`mixed` workers dispatch through a
    // real engine, forcing concurrent LibraryCache::at() (dlopen + map insert, audit item G3).
    // This is the one path that depends on an external engine and is off by default.
    if(!cfg.enginePath.empty()) {
      setupCtx = boss::initialize_boss_context();
      if(setupCtx == nullptr) {
        std::cerr << "Failed to initialize setup context for --engine\n";
        return 1;
      }
      sexp const setupEnv = sexp_context_env(setupCtx);
      boss::evaluate_expression(setupCtx, setupEnv,
                                "SetDefaultEnginePipeline(\"" + cfg.enginePath + "\")", false);
    }

    if(cfg.mode == Mode::Shared) {
      // One context, shared by every thread — the known-unsafe baseline.
      sexp const ctx = boss::initialize_boss_context();
      if(ctx == nullptr) {
        std::cerr << "Failed to initialize shared context\n";
        return 1;
      }
      boss::BossContextGuard const guard {ctx};
      sexp const env = sexp_context_env(ctx);

      std::vector<std::thread> workers;
      workers.reserve(static_cast<size_t>(cfg.threads));
      for(int t = 0; t < cfg.threads; ++t) {
        workers.emplace_back([&, t] { runWorker(ctx, env, suite, t, cfg, counts[t]); });
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
          sexp const ctx = boss::initialize_boss_context();
          if(ctx == nullptr) {
            std::cerr << "thread " << t << ": failed to initialize context\n";
            counts[t].err = static_cast<std::uint64_t>(cfg.iters);
            return;
          }
          boss::BossContextGuard const guard {ctx};
          sexp const env = sexp_context_env(ctx);
          runWorker(ctx, env, suite, t, cfg, counts[t]);
        });
      }
      for(auto& w : workers) {
        w.join();
      }
    }
  }

  auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  if(setupCtx != nullptr) {
    sexp_destroy_context(setupCtx);
  }

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
            << "(data races, if any, are reported by ThreadSanitizer above; triage them in "
               "Tests/Concurrency/known-races.md)\n"
            << std::flush;

  return exitCode;
} catch(std::exception const& e) {
  std::cerr << "fatal: " << e.what() << "\n";
  return 1;
}
