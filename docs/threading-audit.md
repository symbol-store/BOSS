# BOSS Core Thread-Safety Audit

> **Scope:** BOSS *core* only — `Source/*` excluding engine implementations (engines live
> in separate repos, loaded via `dlopen`). Engines are treated as opaque functions; this
> document characterizes what core does on either side of an engine call, not what an
> engine does internally.
>
> **Status:** Phase 1 inventory. This is a reference to grep when reviewing PRs that touch
> shared state. Each entry cites `file:line` so claims are checkable.
>
> **How this was derived:** by reading the core headers/sources listed below and the pinned
> chibi-scheme 0.12 source (`CMakeLists.txt:133`, SHA-pinned). Where a conclusion depends
> on a chibi build flag, the flag and its observed default are cited so the assumption is
> auditable.

## TL;DR — the threading model core actually has today

Core has **exactly three** pieces of shared mutable state, and all three hang off a single
process-wide object:

1. A function-local `static boss::engines::BootstrapEngine engine;` constructed on the first
   call to `BOSSEvaluate` (`Source/BOSS.cpp:32`). There is **one** of these per process.
   Every C caller (`BOSSEvaluate`, `boss::evaluate`), every REPL session, and every
   per-thread chibi context ultimately routes through it.
2. `BootstrapEngine::defaultEngine` (a `std::vector<std::string>`) — the active engine
   pipeline. Read on every root evaluation, rewritten by `SetDefaultEnginePipeline` /
   `ResetEngines`.
3. `BootstrapEngine::libraries` (a `LibraryCache`, i.e. an `unordered_map`) — the engine
   registry. **Lazily mutated *during* evaluation** (first use of an engine path `dlopen`s
   and inserts).

As originally inventoried, none of the three was synchronized. **Phase 4 has since added an
`engineStateMutex` guarding G2 + G3** (see "Implications" below); the description here is the
hazard the inventory found and Phase 4 fixed. Everything else in core is either **per-context**
(chibi state) or **caller-owned** (the C value API, serialization buffers) and is safe under
the *"one context per concurrent caller"* rule.

**The load-bearing finding:** *per-thread context ≠ per-thread engine.* A caller can give
every thread its own `sexp` context (isolating all chibi state — see §3), and the moment any
of those threads dispatches a query it crosses the FFI boundary
(`BOSSEvaluate`, `chibi-bindings.stub:44`) into the **shared** singleton engine and its
registry/pipeline. Context isolation buys parsing/GC isolation; it buys **nothing** for engine
dispatch. This was why the per-thread-context configuration was not race-free — the gap Phase 4
closed by synchronizing the singleton's shared state without serializing engine calls.

---

## 1. Globals & file-scope statics (`Source/*`, excluding engine impls)

| # | Location | Symbol | What it is | Lifetime / owner | Synchronization | Risk |
|---|----------|--------|------------|------------------|-----------------|------|
| G1 | `BOSS.cpp:32` | `engine` | function-local `static BootstrapEngine` — the singleton dispatcher | process lifetime; constructed on first `BOSSEvaluate` | none (C++11 guarantees thread-safe *construction* only) | **HIGH** — central shared object; holds G2 + G3. Every thread/context dispatches through this one instance. |
| G2 | `BootstrapEngine.hpp:116` | `BootstrapEngine::defaultEngine` | `std::vector<std::string>` engine pipeline | member of G1 | none | **HIGH** — read on every root `evaluate()` (`BootstrapEngine.hpp:244`); cleared/rewritten by `SetDefaultEnginePipeline` (`:155`) and `ResetEngines` (`:202`). Read/write data race + iterator invalidation. |
| G3 | `BootstrapEngine.hpp:114` | `BootstrapEngine::libraries` | `LibraryCache` (private `unordered_map<string, LibraryAndFunctions>`) | member of G1 | none | **HIGH** — see §2. Mutated mid-evaluation via lazy `dlopen`. |
| G4 | `BootstrapEngine.hpp:118` | `registeredOperators` | `const unordered_map<Symbol, function<...>>` | member of G1 | const after construction | LOW — immutable; concurrent reads safe. **But** its closures capture `this` and touch G2/G3, so the hazard is in G2/G3, not the map itself. |
| G5 | `Expression.hpp:199` | `typenames` | function-local `static std::map<type_index,const char*>` | first-use, process lifetime | C++11 thread-safe static init | LOW/benign — error-path only, read-only after init. |
| G6 | `ExpressionUtilities.hpp:122` | `operator""_` | `static` UDL **function** (internal linkage) | — | n/a | NONE — function, not state. |
| G7 | `PortableBOSSSerialization.h:44,48` | `..._RLE_MINIMUM_SIZE`, `..._RLE_BIT` | `static size_t const` constants | — | n/a | NONE — compile-time constants (internal linkage in a header). |
| G8 | `PortableBOSSSerialization.h:86–312` | `make*Argument`, `storeString`, `viewString`, … | `static` **functions** operating on a caller-supplied `root` | — | n/a | NONE — no global state; all state lives in the caller-owned `PortableBOSSRootExpression` buffer. Reentrant per distinct `root`. |

Notes:
- `Serialization.hpp` statics (`:27,80,93,117,…`) are all `static constexpr` or `static`
  *member functions* — no shared mutable state. `Serialization.cpp` is a 29-byte stub.
  **Serialization is reentrant as long as each call owns its `root` buffer**, which the API
  enforces (the buffer is returned to / supplied by the caller).
- No file-scope namespace-level mutable variables exist in core. The Windows `dlopen`/`dlsym`
  shims (`BootstrapEngine.hpp:19–53`) are `static` functions, not state.

## 2. Engine registry (`LibraryCache`)

**There is no central registration table.** Engines are shared libraries identified by
filesystem path. The only registry is `BootstrapEngine::libraries`
(`BootstrapEngine.hpp:77–114`), private to the singleton engine (hence effectively
process-global).

- **Lookup is lazy and mutating.** `LibraryCache::at(path)` (`BootstrapEngine.hpp:78`): on a
  cache miss it `dlopen(path, RTLD_NOW | RTLD_NODELETE)`, `dlsym`s `evaluate` (required) and
  `reset` (optional), then `emplace`s into the map. Hit → returns cached entry.
- **Mutated *during* evaluation: YES.** `at()` is called from inside the `EvaluateInEngines`
  (`:126`) and `GetEngineDescription` (`:177`) operators, which run as part of
  `evaluate()`. So evaluation is **not** a read-only operation against the registry — the
  first query that names a not-yet-loaded engine inserts into the map while other threads may
  be reading or inserting.
  - Concurrent inserts → data race on the `unordered_map` (possible rehash) and a possible
    **double `dlopen`** of the same path.
- **Handle lifetime.** Handles live in the map for the life of the singleton. `RTLD_NODELETE`
  means the library stays mapped even after `dlclose`. `LibraryCache::clear()`
  (`:99`, run by `~LibraryCache` and by `ResetEngines` at `:203`) calls each engine's `reset`
  then `dlclose`, then empties the map.
- **Teardown races dispatch.** `ResetEngines` can `clear()` the cache (calling engine `reset`
  + `dlclose`) concurrently with another thread mid-dispatch through an entry. Even with
  `RTLD_NODELETE` keeping code mapped, `reset()` mutating engine-internal state under an
  in-flight call is a use-after-reset hazard. (Engine-internal effects are out of scope, but
  *core* hands the engine a `reset` while another core thread is calling `evaluate` — that
  sequencing is core's responsibility.)
- **Synchronization: none.**
- The dynamic loader (`dlopen`/`dlclose`) is itself thread-safe on glibc/macOS; the
  **unsynchronized map mutation around it is not**. Loading an engine also runs its static
  initializers on whichever thread first touches it — noted for engine maintainers, not
  fixed here.

## 3. Chibi-scheme context

A `BossContext` is a `sexp ctx` produced by `initialize_boss_context()`
(`ExpressionParser.hpp:237`) via `sexp_make_eval_context`, owned by an RAII
`BossContextGuard` (`:30`, non-copyable/non-movable, `sexp_destroy_context` on scope exit).

**Per-context state — unsafe to share a single `ctx` across threads, safe across distinct
contexts:**
- GC root stack (`sexp_gc_var*` / `sexp_gc_preserve*` / `sexp_gc_release*`), used throughout
  `expr_to_sexp` (`:61`), `eval_string` (`:222`), `evaluate_expression` (`:275`), and the
  REPL.
- Eval stack, environment chain (`:255`), I/O ports (`:254`), and the bump allocator/heap.

All of the above are created per `sexp_make_eval_context` and are **not internally
synchronized** — two threads in the same `ctx` race on the root stack and allocator. Hence the
contract: **one context per concurrent caller.**

**Process-global chibi state — verified against pinned chibi 0.12, default build flags:**
- The CMake build (`CMakeLists.txt:132–141`) passes **no** `-DSEXP_USE_*` flags. In chibi's
  `features.h`, `SEXP_USE_BOEHM` and `SEXP_USE_MALLOC` both default to `0`
  (`features.h:507,527`). Consequently:
  - `SEXP_USE_GLOBAL_HEAP` → `0` (`features.h:602–608`) ⇒ **heap is per-context.** ✓
  - `SEXP_USE_GLOBAL_SYMBOLS` → `0` (`features.h:610–616`) ⇒ **symbol table is per-context.** ✓
    So `sexp_intern` (`ExpressionParser.hpp:57,69,214,…`) on *distinct* contexts does not race.
- The **only** process-global chibi state in this configuration is the one-time init guard
  `static int sexp_initialized_p` (chibi `sexp.c:22`), set inside `sexp_init()`
  (`sexp.c:4025`, reached via `sexp_scheme_init()` which BOSS calls at the top of **every**
  `initialize_boss_context()`, `ExpressionParser.hpp:238`). It guards `sexp_gc_init()` and —
  only under `GLOBAL_SYMBOLS`, which is off here — the symbol-table zeroing.
  - **Risk: init-time race only.** Two threads making their *first* `initialize_boss_context()`
    call concurrently race on `sexp_initialized_p` (an unsynchronized read-modify-write) and
    on the one-time init it guards. After the first init it is a benign read.
  - **Mitigation (cheap):** warm up once — call `initialize_boss_context()` (or bare
    `sexp_scheme_init()`) on the main thread *before* spawning workers, or wrap the first init
    in `std::call_once`. No per-call locking needed.

> ⚠️ **Build-flag assumption to pin.** If chibi is ever rebuilt with `SEXP_USE_BOEHM` or
> `SEXP_USE_MALLOC` (e.g. to debug the native GC — see `features.h:75–97`), both
> `SEXP_USE_GLOBAL_SYMBOLS` and `SEXP_USE_GLOBAL_HEAP` flip to `1`, making the symbol table
> and heap **process-global**. At that point `sexp_intern` and allocation race *across*
> contexts and the per-thread-context model breaks entirely. The concurrency tests (Phase 2)
> should assert/encode this build assumption.

**Cross-layer hazard (the one that matters):** the `boss-eval` macro
(`ExpressionParser.hpp:157–163`) expands to a call to the `BOSSEvaluate` FFI binding
(`Shims/chibi-bindings.stub:44`), which lands in the single `static BootstrapEngine`
(G1). Fully isolated per-thread chibi contexts therefore still converge on shared,
unsynchronized core state (§1/§2) the instant a query dispatches. **Context isolation does not
imply dispatch isolation.**

**Context destruction owned the host's standard streams (found + fixed in Phase 2).**
`initialize_boss_context()` originally called
`sexp_load_standard_ports(ctx, env, stdin, stdout, stderr, /*no_close=*/0)`
(`ExpressionParser.hpp:254`). With `no_close=0`, chibi marks the `stdin`/`stdout`/`stderr`
ports close-on-finalize, so `sexp_destroy_context` → `sexp_finalize_port` → `fclose(stdout)`
(chibi `sexp.c:227`). For the single-context REPL this is harmless (it dies at process exit),
but for the *many-contexts* model this audit targets it is a real defect: destroying any
context `fclose`s the host application's stdio, and in the per-thread-context configuration
every `BossContextGuard` destruction `fclose`s the **shared** `FILE*` — a data race plus a
double `fclose` (undefined behavior) across threads. Fixed by passing `no_close=1` (a library
context must not take ownership of the process's standard streams). Tracked as
`known-races.md` R5.

## 4. Named-handle storage / other cross-call state

- Searched core for `Name` / `ByName` / global handle or interning registries: **none.**
  `Symbol::getName()` is a value accessor; there is no C++-side global symbol/name table
  (interning is per chibi `ctx`, §3).
- The C API hands out raw **owning** pointers (`BOSSExpression*`, `BOSSSymbol*`, and `char*`
  from `strdup` in `BOSS.cpp:86,142,145`). These are caller-owned with explicit `free*`
  functions; no shared registry retains them.
- Between `BOSSEvaluate` calls, the **only** state core retains is the singleton engine itself
  (G1 → G2 + G3). There is no other cross-call core state.

---

## Implications for later phases

- **Phase 4 (dispatch contract) — implemented.** Decision: engines are assumed reentrant and
  concurrent evaluation is explicitly desired, so the dispatcher must **not** serialize engine
  calls. `BootstrapEngine` now guards G2 (`defaultEngine`) and G3 (`libraries`) with a single
  `std::shared_mutex` (`engineStateMutex`), under one invariant: **the lock is never held
  across an engine call.** The dispatch path snapshots the pipeline and resolves engine
  function pointers under a *shared* lock, releases it, then calls engines lock-free
  (`RTLD_NODELETE` keeps resolved pointers valid). First-use `dlopen` upgrades to an *exclusive*
  lock (double-checked). Reconfiguration (`SetDefaultEnginePipeline`/`ResetEngines`) takes the
  exclusive lock and is valid only under a **quiesce** contract (no eval in flight) — core keeps
  its own state consistent, but `ResetEngines` resets+unloads engines, which is unsafe under a
  concurrent in-flight call regardless of locking; `ResetEngines` does the reset/unload outside
  the lock. The per-engine `EngineThreadingPolicy` enum was **dropped** (all engines assumed
  reentrant; re-add if a non-thread-safe engine appears). The full contract lives at the top of
  `Source/BootstrapEngine.hpp`. Verified by the `--direct --suite pipeline` gate (R1 reproducer,
  now TSan-clean). The discipline collapses to one mutex with no lock ordering, so **Phase 6
  (formal modeling) is not warranted.**
- **Phase 2 (stress harness) — built; results below.** `Tests/Concurrency/stress.cpp` +
  the `boss_concurrency_stress` ThreadSanitizer target. Empirically (Apple clang 21 / arm64,
  see `Tests/Concurrency/known-races.md`):
  - **per-thread-context + pure: CLEAN** (8000+ evals, 0 races) — confirms chibi is
    per-context isolated in this build, validating §3.
  - **shared-context + pure: RACE** on the shared ctx heap, then crash — confirms the
    "one context per caller" contract is mandatory (R3).
  - **G1/G2 confirmed (R1):** the `defaultEngine` race is real, reported by TSan directly on
    the `BOSSEvaluate::engine` singleton. It only surfaces through **direct** dispatch
    (`--direct`); the chibi path buries the access window under parse/GC work, so a clean
    chibi-path pipeline run is *not* evidence the race is gone.
  - **G3 (`libraries`) still needs a loadable engine fixture** (`--engine`) to exercise the
    `dlopen` insert; predicted, not yet reproduced.
  - The §3 init-time race is avoided by a one-context warm-up before the worker threads start.
- **Phase 5 (defensive instrumentation) — implemented.** Two mechanisms in
  `Source/ThreadSafety.hpp`:
  - **`ConcurrencyTripwire`** — a debug-build-only (`!NDEBUG`) RAII guard at the top of
    `evaluate_expression` (`ExpressionParser.hpp`). It maintains a process-global
    `ctx → {owning thread, depth}` registry; if a second thread enters an evaluation on a `ctx`
    already owned by another thread, it prints a diagnostic and `std::abort()`s — converting the
    silent corruption of a shared chibi context into an immediate, loud crash. Same-thread
    re-entry is depth-counted and allowed, so ordinary nested evaluation never trips it. In
    release builds it compiles to an empty no-op object. Verified: the harness's `--mode shared`
    aborts with the tripwire message in a Debug build; `--mode per-thread` does not. (The engine
    singleton — the other candidate site — is now lock-protected by Phase 4, so the per-`ctx`
    tripwire is the remaining silent-corruption hazard worth guarding.)
  - **Clang Thread Safety Analysis** — `defaultEngine` and `libraries` are `GUARDED_BY` an
    annotated `SharedMutex` (`engineStateMutex`); `SharedLock`/`UniqueLock` guards carry the
    acquire/release capability attributes. The build enables `-Wthread-safety
    -Werror=thread-safety` (Clang-only; macros are no-ops elsewhere), so any unguarded access to
    the guarded state fails the build. This statically enforces the Phase 4 discipline; it
    already caught one real gap (a callback-boundary write in `SetDefaultEnginePipeline`) during
    development, which was fixed.

## Quick grep map

| Looking for… | Where |
|---|---|
| The singleton engine | `BOSS.cpp:32` |
| Engine-state mutex (Phase 4) | `BootstrapEngine.hpp:105` (`engineStateMutex`) |
| Engine pipeline mutation | `BootstrapEngine.hpp:209` (`SetDefaultEnginePipeline`), `:268` (`ResetEngines`) |
| Lazy dlopen / registry insert | `BootstrapEngine.hpp:162` (`resolveEvaluateFunction`), `:118` (`LibraryCache::loadOrGet`) |
| Registry teardown | `BootstrapEngine.hpp:138` (`detachAll`) + `:91` (`unloadLibraries`, runs outside the lock) |
| Threading annotations / guards | `Source/ThreadSafety.hpp` (`SharedMutex`, `SharedLock`, `GUARDED_BY`) |
| Concurrency tripwire | `Source/ThreadSafety.hpp` (`ConcurrencyTripwire`), used at `ExpressionParser.hpp:282` |
| Thread-safety build gate | `CMakeLists.txt` (`-Wthread-safety -Werror=thread-safety`, Clang-only) |
| FFI dispatch boundary | `Shims/chibi-bindings.stub:44`, `ExpressionParser.hpp:163` |
| Context creation / destruction | `ExpressionParser.hpp:237` (`initialize_boss_context`), `:36` (`~BossContextGuard`) |
| Standard-streams ownership fix (R5) | `ExpressionParser.hpp:259` (`no_close=1`) |
| chibi global init guard | chibi `sexp.c:22`, `:4025` |
| chibi global-heap/symbols flags | chibi `features.h:602–616` (depend on `:507,527`) |
