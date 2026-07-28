# BOSS Concurrency — Known Races (TSan triage log)

This is the living triage log for ThreadSanitizer reports from `boss_concurrency_stress`
(see `Tests/Concurrency/stress.cpp` and `docs/threading-audit.md`). Every report goes in the
table below with a **classification**, an **in-scope** flag, and a **status**.

## Scope rule (which races are *ours*)

This work hardens **BOSS core**. Classify each report:

- **core** — the racy memory lives in core (`Source/*` excluding engine impls): the singleton
  `BootstrapEngine` and its `defaultEngine` / `libraries`, serialization, the C value API.
  **In scope.** Drive these to zero (Phase 4 / Phase 5).
- **chibi-per-context** — the race is on chibi context state (GC root stack, heap, symbol
  table) caused by *sharing one `ctx` across threads*. This is the `--mode shared` baseline
  and is **expected**; it is *not* a core defect but the demonstration that the
  "one context per caller" rule is mandatory. In scope only as a baseline to confirm.
- **engine-visible** — the race is inside an engine, observed *through* a dispatch call.
  **Out of scope** — file it for the engine maintainer; do not fix here.

## Instrumentation note (important when reading reports)

The `boss_concurrency_stress` target is built with `-fsanitize=thread`, but the **chibi static
library is not instrumented** (it is built by its own `ExternalProject`). Consequences:

- Races on memory touched only by chibi's C internals may be **invisible** to TSan.
- BUT the GC-root-stack macros `sexp_gc_var*` / `sexp_gc_preserve*` / `sexp_gc_release*`
  expand to pointer writes on `sexp_context_saves(ctx)` that execute in **instrumented header
  code** (`ExpressionParser.hpp`, `BOSSREPL.hpp`-style call sites). So the `--mode shared`
  baseline *does* light up TSan via those macros even without an instrumented chibi.
- To chase chibi-internal races directly, rebuild chibi with `-fsanitize=thread` (pass TSan
  `CFLAGS`/`LDFLAGS` into the `chibi-scheme` `ExternalProject_Add` build/install commands).
  Not done by default — out of scope for core hardening.

Also pin the chibi build assumption (audit §3): this analysis holds only while chibi is built
with **neither** `SEXP_USE_BOEHM` nor `SEXP_USE_MALLOC` (so heap + symbol table are
per-context). If that changes, per-thread-context stops being isolatable and new
chibi-per-context races appear that are *not* fixable in core.

## How to reproduce

```sh
# clean gate — must be race-free:
TSAN_OPTIONS="halt_on_error=1 abort_on_error=1" \
  ./boss_concurrency_stress --mode per-thread --suite pure --threads 8 --iters 2000

# baseline — expected to report a race (and may crash):
TSAN_OPTIONS="halt_on_error=0" \
  ./boss_concurrency_stress --mode shared --suite pure --threads 8 --iters 500

# dispatch + reconfiguration race check — CLEAN since Phase 4 (was the R1 reproducer). Use
# --direct: the chibi path buries the dispatch race under parse/GC work; direct dispatch is
# what reliably surfaced it before the fix.
TSAN_OPTIONS="halt_on_error=1 abort_on_error=1" \
  ./boss_concurrency_stress --direct --suite pipeline --threads 8 --iters 5000
```

---

## Triage table

> Status legend: **PREDICTED** = derived from the audit, not yet confirmed by a TSan run ·
> **REAL** = confirmed by TSan · **BENIGN** = confirmed race but provably harmless ·
> **SUPPRESSED** = entry in `tsan_suppressions.txt`, with reason · **FIXED** = resolved.

| ID | Config that surfaces it | Racy state (audit ref) | Top frames (signature) | Class | In scope | Status | Notes |
|----|-------------------------|------------------------|------------------------|-------|----------|--------|-------|
| R1 | `--direct --suite pipeline` | `BOSSEvaluate::engine` → `defaultEngine` (G1/G2) | write in `SetDefaultEnginePipeline` operator vs read at `BootstrapEngine.hpp:244` (`!defaultEngine.empty()`) | core | yes | **FIXED** | The headline core race. Was REAL on the process-wide singleton. Fixed by the Phase 4 `engineStateMutex` (snapshot pipeline under a shared lock; reconfig under exclusive). Reproducer now TSan-clean across repeated runs. See detail R1 below. |
| R2 | `--direct --suite pipeline --engine <path>` | `BootstrapEngine::libraries` `unordered_map` (G3) | lazy `dlopen` insert (`loadOrGet`) vs lookup (`tryGet`); possible double `dlopen` | core | yes | **FIXED (by design)** | Same `engineStateMutex` guards `libraries`: `resolveEvaluateFunction` does shared-lock lookup, exclusive-lock double-checked `dlopen` on miss. Not independently reproduced (needs a loadable engine fixture), but covered by the Phase 4 lock. |
| R3 | `--mode shared --suite pure` | shared chibi ctx heap / GC root stack (audit §3) | write/write in `evaluate_expression` (`ExpressionParser.hpp:280`) on the shared ctx heap | chibi-per-context | baseline only | **REAL** | Expected. Sharing a `ctx` is unsafe — the contract, not a core bug. Vanishes in `--mode per-thread`. Crashes (DEADLYSIGNAL) shortly after. See detail R3 below. |
| R4 | `--mode per-thread --no-warmup` | chibi `sexp_initialized_p` (audit §3) | `sexp_init` RMW of the global init flag | core-adjacent (chibi global) | yes (mitigation) | **PREDICTED** | Init-time only. Mitigated by the harness warm-up (default on) and, for real callers, a one-time init before threads start. |
| R5 | any context create+destroy (found via harness warm-up) | process `stdin`/`stdout`/`stderr` `FILE*` | `sexp_finalize_port` `fclose` on context destroy (`initialize_boss_context` passed `no_close=0`) | core | yes | **FIXED** | Destroying a context fclosed the host's stdio (and double-fclosed it under concurrent destruction). Fixed in `ExpressionParser.hpp` by `no_close=1`. See detail R5 below. |

Notes on what did **not** surface:
- **R1 via the chibi path** (`--mode per-thread --suite pipeline`) ran clean across 24k+ evals.
  The race is real (R1 confirms it) but each chibi `evaluate_expression` spends almost all its
  time in parse/GC, so the `defaultEngine` access window rarely overlaps across threads. This
  is why the core-race tracker uses `--direct`. A clean chibi-path pipeline run is **not**
  evidence the race is fixed — only the `--direct` run is authoritative for R1.

When a real TSan run confirms or refutes a row, update its **Status**, paste the trimmed TSan
stack into a per-ID subsection below, and (if out of scope or benign) add a
`tsan_suppressions.txt` entry referencing the ID.

## Confirmed report details

### R1 — singleton engine `defaultEngine` race (REAL, core, in scope)

`--direct --suite pipeline --threads 8 --iters 5000`, Apple clang 21 / arm64. Exit 134.

```
WARNING: ThreadSanitizer: data race
  Write of size 8 by thread T1:
    #0 BootstrapEngine::registeredOperators[SetDefaultEnginePipeline]::operator()  function.h:174
    #2 BootstrapEngine::evaluate(...)            BootstrapEngine.hpp:249
    #3 BOSSEvaluate                              BOSS.cpp:33
    #4 boss::evaluate(...)                       BOSS.cpp:184
  Previous read of size 8 by thread T4:
    #0 BootstrapEngine::evaluate(...)            BootstrapEngine.hpp:244   // !defaultEngine.empty()
    #1 BOSSEvaluate                              BOSS.cpp:33
  Location is global 'BOSSEvaluate::engine' at 0x...   // the function-local static singleton
```

Write to `defaultEngine` (in the `SetDefaultEnginePipeline`/`ResetEngines` operators) races
with the wrap-check read at `BootstrapEngine.hpp:244`, on the one process-wide engine. This is
audit items G1+G2.

**Fixed (Phase 4):** `BootstrapEngine` now guards `defaultEngine` and `libraries` with a
`std::shared_mutex` (`engineStateMutex`). The dispatch path snapshots the pipeline and resolves
engine function pointers under a *shared* lock and **releases it before any engine call**;
reconfiguration (`Set`/`ResetEngines`) takes the *exclusive* lock. Engine calls run lock-free,
so concurrent evaluation is preserved (engines are assumed reentrant). The reproducer above is
now TSan-clean. This is the canonical regression test for the fix and is a hard gate in CI.

### R3 — shared context heap race (REAL, chibi-per-context, baseline only)

`--mode shared --suite pure --threads 4 --iters 200`. Exit 134 (race then DEADLYSIGNAL).

```
WARNING: ThreadSanitizer: data race
  Write of size 8 by thread T2:
    #0 boss::evaluate_expression(...)            ExpressionParser.hpp:280
  Previous write of size 8 by thread T1:
    #0 boss::evaluate_expression(...)            ExpressionParser.hpp:280
  Location is heap block of size 2097232 ... allocated by main thread:
    #1 sexp_make_heap                            gc.c:589
    #2 main                                      stress.cpp   // initialize_boss_context() of the shared ctx
```

Two threads mutate the same context's heap / GC root stack via the `sexp_gc_*` macros. This is
the demonstration that the *"one context per caller"* contract is mandatory, not a core defect.
It disappears in `--mode per-thread` (the clean gate passes).

### R5 — context destruction closes the host's standard streams (REAL, core, FIXED)

Found immediately by the harness warm-up: creating a context and destroying it terminated all
further program output. `initialize_boss_context()` called
`sexp_load_standard_ports(ctx, env, stdin, stdout, stderr, /*no_close=*/0)`, so chibi marked
those ports close-on-finalize; `sexp_destroy_context` → `sexp_finalize_port` →
`fclose(stdout)` (chibi `sexp.c:227`). In the per-thread-context target configuration, every
`BossContextGuard` destruction `fclose`s the *shared* `FILE*` — a data race and a double
`fclose` (undefined behavior) across threads, plus it corrupts the host application's stdio.

Fix: `ExpressionParser.hpp` now passes `no_close=1`. A library context must not take ownership
of the process's standard streams. Verified: with the fix, `--mode per-thread --suite pure`
runs 8000+ evals clean.
