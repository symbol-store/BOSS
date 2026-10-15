# BOSS Core Thread Safety

This document lists the state that BOSS core shares between threads and describes how BOSS core
synchronizes access to that state. Each entry names the function or variable that a reader can
search for.

## Terms

- **BOSS core**: the code in `Source/`.
- **Engine**: a shared library that BOSS core loads with `dlopen` and calls through the library's
  `evaluate` function and, if the library exports one, its `reset` function. Engines are not part
  of BOSS core. This document describes what BOSS core does before and after it calls an engine,
  and not what an engine does inside that call.
- **Dispatch**: a call from BOSS core into an engine's `evaluate` function.
- **Engine pipeline**: the ordered list of engines to which `BootstrapEngine` dispatches an
  expression.
- **Bootstrap command**: an expression whose head is one of the operators in
  `BootstrapEngine::registeredOperators`, such as `SetDefaultEnginePipeline`. `BootstrapEngine`
  evaluates a bootstrap command itself, without a dispatch.
- **Root evaluation**: a call to `BootstrapEngine::evaluate` with `isRootExpression` set, which is
  the default. The calls that `BootstrapEngine::evaluate` makes on the arguments of an expression
  pass `false`.
- **Reconfiguration**: an evaluation of `SetDefaultEnginePipeline` or `ResetEngines`. Both
  operators replace the engine pipeline, and both reset and unload every loaded engine.
- **In flight**: `BootstrapEngine` counts a dispatch as in flight from the moment a root
  evaluation that is not a bootstrap command copies the engine pipeline, or the
  `EvaluateInEngines` operator starts, until that evaluation returns. The counter is
  `evaluationsInFlight`.
- **chibi context**: a `sexp` value that `sexp_make_eval_context` creates. `initialize_boss_context`
  creates one, and `BossContextGuard` destroys it with `sexp_destroy_context` when the
  `BossContextGuard` object goes out of scope.

The findings below come from reading the BOSS core sources and the source of chibi-scheme 0.12,
which `CMakeLists.txt` downloads with a fixed SHA-256 hash. Where a finding depends on a chibi
build flag, this document gives the flag and the flag's default value.

## Summary

BOSS core shares the following state between threads:

1. The `BootstrapEngine` object `engine`, a function-local static in `BOSSEvaluate`
   (`Source/BOSS.cpp`). The C++ runtime constructs `engine` on the first call to `BOSSEvaluate`.
   Every caller of `BOSSEvaluate`, and so every chibi context, evaluates through this one object.
2. `BootstrapEngine::defaultEnginePipeline`, the engine pipeline. Every root evaluation that is not
   a bootstrap command reads it, and `SetDefaultEnginePipeline` and `ResetEngines` replace it.
3. `BootstrapEngine::libraries`, a `LibraryCache` that maps a library path to the loaded library
   and its `evaluate` and `reset` functions. The first dispatch to an engine loads the engine's
   library and inserts it into the map, so an evaluation can change the map.
4. Four static members of `BootstrapEngine` that PR #195 adds: the mutex `engineStateMutex`, the
   counter `evaluationsInFlight`, the flag `reconfigurationInProgress` and the vector
   `librariesAwaitingTeardown`. Sections 2 and 5 describe them.

Before PR #195, BOSS core synchronized none of this state. PR #195 makes
`defaultEnginePipeline`, `libraries` and `librariesAwaitingTeardown` accessible only under
`engineStateMutex`. `BootstrapEngine` takes `engineStateMutex` to copy the engine pipeline and to
look up engine functions, and releases `engineStateMutex` before every call into an engine, so
dispatches from different threads run at the same time.

All other state that BOSS core uses is either part of one chibi context (section 3) or memory
that the caller allocates and frees (section 4). Threads can therefore use BOSS core
concurrently as long as no two threads use the same chibi context.

Separate chibi contexts separate the chibi state of the threads, but not their dispatches: every
chibi context evaluates a query by calling `BOSSEvaluate` (section 3), and `BOSSEvaluate`
evaluates through the single `engine` object. Before PR #195, threads with separate chibi contexts
therefore had data races on `defaultEnginePipeline` and `libraries`.

---

## 1. Process-wide variables in BOSS core

| Location | Symbol | What it is | Synchronization |
|----------|--------|------------|-----------------|
| `BOSS.cpp`, `BOSSEvaluate` | `engine` | function-local `static BootstrapEngine` | C++11 makes the construction thread-safe. The members below are synchronized as listed. |
| `BootstrapEngine.hpp` | `BootstrapEngine::defaultEnginePipeline` | `std::vector<Expression>`, the engine pipeline | `BOSS_GUARDED_BY(engineStateMutex)` |
| `BootstrapEngine.hpp` | `BootstrapEngine::libraries` | `LibraryCache`, a private `unordered_map<string, LibraryAndFunctions>` | `BOSS_GUARDED_BY(engineStateMutex)` |
| `BootstrapEngine.hpp` | `BootstrapEngine::librariesAwaitingTeardown` | static `vector<LibraryAndFunctions>` | `BOSS_GUARDED_BY(engineStateMutex)` |
| `BootstrapEngine.hpp` | `BootstrapEngine::evaluationsInFlight`, `BootstrapEngine::reconfigurationInProgress` | static atomics | atomic; section 5 explains which lock each access also holds |
| `BootstrapEngine.hpp` | `BootstrapEngine::registeredOperators` | `const unordered_map<Symbol, function<...>>` | Not modified after construction, so concurrent reads are safe. The functions in the map capture `this` and access the pipeline and the library cache under `engineStateMutex`. |
| `Expression.hpp` | `typenames` | function-local `static std::map<type_index, const char*>` | C++11 makes the initialization thread-safe, and nothing modifies the map afterwards. Only error messages read it. |

The remaining `static` declarations in BOSS core declare functions or constants, not variables:

- `operator""_` in `ExpressionUtilities.hpp` is a `static` function.
- `PortableBOSSSerialization.h` declares `static size_t const` constants and `static` functions
  (`make*Argument`, `storeString`, `viewString` and others). The functions read and write only the
  `PortableBOSSRootExpression` buffer that the caller passes, so two threads can call them
  concurrently on different buffers.
- The `static` declarations in `Serialization.hpp` are `static constexpr` values and `static`
  member functions.
- The Windows replacements for `dlopen` and `dlsym` at the top of `BootstrapEngine.hpp` are
  `static` functions.

BOSS core has no other mutable variables at namespace scope.

## 2. Engine library cache (`LibraryCache`)

BOSS core has no table in which engines register themselves. A library path identifies an
engine, and `BootstrapEngine::libraries` is the only map from paths to loaded engines. Because
`engine` in `BOSSEvaluate` is the only `BootstrapEngine` in a typical process, this map is in
effect process-wide.

- **Lookup.** `resolveEvaluateFunction` takes `engineStateMutex` shared and calls
  `LibraryCache::tryGet`. If the library is not loaded yet, `resolveEvaluateFunction` releases
  the shared lock, takes `engineStateMutex` exclusively and calls `LibraryCache::loadOrGet`.
  `loadOrGet` checks the map again, because another thread can load the library between the two
  locks. If the library is still missing, `loadOrGet` calls
  `dlopen(path, RTLD_NOW | RTLD_NODELETE)`, looks up `evaluate` (required) and `reset` (optional)
  with `dlsym`, and inserts the result into the map.
- **Before PR #195,** `LibraryCache::at` inserted into the map without a lock. Two threads that
  dispatched to the same unloaded engine at the same time had a data race on the
  `unordered_map`, including a possible rehash, and could both call `dlopen` on the same path.
- **Function pointers outside the lock.** `BootstrapEngine` calls an engine's `evaluate` after it
  has released `engineStateMutex`. `RTLD_NODELETE` makes the dynamic loader keep a library mapped
  even after `dlclose`, so a function pointer that `BootstrapEngine` looked up under the lock
  remains valid. On Windows, `BootstrapEngine.hpp` defines `RTLD_NODELETE` as 0.
- **Teardown.** `LibraryCache::detachAll` empties the map under the exclusive lock and returns
  the removed libraries. `unloadLibraries` then calls each library's `reset` and `dlclose` without
  holding the lock, because `reset` is a call into the engine. If a `reset` throws,
  `unloadLibraries` still resets and unloads the remaining libraries, and rethrows the first
  exception afterwards.
- **Reconfiguration during a dispatch.** Before PR #195, `ResetEngines` could reset and unload an
  engine while another thread was inside that engine's `evaluate`. `RTLD_NODELETE` keeps the code
  mapped, but `reset` changes the engine's state during the other thread's call. Section 5
  describes how PR #195 refuses such a reconfiguration.
- **Loading.** `dlopen` and `dlclose` are thread-safe on glibc and macOS. Loading an engine runs
  the engine's static initializers on the thread that first dispatches to it. BOSS core does not
  change this behaviour, and engine authors need to know it.

## 3. chibi contexts

`initialize_boss_context` (`ExpressionParser.hpp`) creates a chibi context with
`sexp_make_eval_context`. A `BossContextGuard` object destroys the context with
`sexp_destroy_context` when the object goes out of scope, and cannot be copied or moved.

**State of one chibi context.** Two threads must not use the same chibi context at the same time.
Different threads can use different chibi contexts. `sexp_make_eval_context` creates a separate
instance of each of the following for every context:

- the GC root stack (`sexp_gc_var*`, `sexp_gc_preserve*`, `sexp_gc_release*`), which
  `expr_to_sexp`, `eval_string`, `evaluate_expression` and the REPL use;
- the evaluation stack, the environment chain, the I/O ports, the heap and the allocator.

chibi does not synchronize access to any of these. Two threads that use one chibi context
therefore have data races on the root stack and the allocator. Hence the rule: **no two threads
evaluate in the same chibi context at the same time.**

**Process-wide chibi state, checked against chibi 0.12 with default build flags.**

- `CMakeLists.txt` passes no `-DSEXP_USE_*` flags to the chibi build. In chibi's `features.h`,
  `SEXP_USE_BOEHM` and `SEXP_USE_MALLOC` both default to 0 (`features.h:507,527`). As a result:
  - `SEXP_USE_GLOBAL_HEAP` is 0 (`features.h:602-608`), so every context has a separate heap.
  - `SEXP_USE_GLOBAL_SYMBOLS` is 0 (`features.h:610-616`), so every context has a separate
    symbol table, and calls to `sexp_intern` on different contexts do not race.
- In this configuration the only process-wide chibi state is `static int sexp_initialized_p`
  (chibi `sexp.c:22`). `sexp_init` (`sexp.c:4025`), which `sexp_scheme_init` calls, reads and
  sets this flag without synchronization, and calls `sexp_gc_init` only when the flag is not
  set yet. Two threads that initialize chibi for the first time at the same time race on the
  flag and on `sexp_gc_init`. `initialize_boss_context` therefore calls `sexp_scheme_init`
  through `std::call_once`.

**Build-flag assumption.** If chibi is built with `SEXP_USE_BOEHM` or `SEXP_USE_MALLOC` set, for
example to debug chibi's garbage collector (`features.h:75-97`), then `SEXP_USE_GLOBAL_SYMBOLS`
and `SEXP_USE_GLOBAL_HEAP` both become 1. All contexts then share one symbol table and one heap,
`sexp_intern` and allocation race across contexts, and separate contexts no longer make
concurrent evaluation safe.

**Dispatch from separate chibi contexts.** `setup_boss_scheme` defines the `boss-eval` macro, which
expands to a call to the `BOSSEvaluate` binding in `Shims/chibi-bindings.stub`. That binding
calls `BOSSEvaluate`, which evaluates through the single `engine` object. Threads with separate
chibi contexts therefore all dispatch through one `BootstrapEngine`. Since PR #195, their
dispatches do not race on `defaultEnginePipeline` and `libraries`, but separate chibi contexts
alone do not make dispatch thread-safe.

**Standard streams.** `initialize_boss_context` used to call
`sexp_load_standard_ports(ctx, env, stdin, stdout, stderr, 0)`. With the last argument
(`no_close`) at 0, chibi closes the three `FILE*` streams when it finalizes their ports, so
`sexp_destroy_context` called `fclose(stdout)` (chibi `sexp.c:227`). With one context per
process this happened only at exit. With one context per thread, every destruction of a context
closed the process's shared `stdin`, `stdout` and `stderr`, which is a data race and a double
`fclose`. `initialize_boss_context` now passes 1, so that chibi does not close the streams.
`Tests/Concurrency/known-races.md`, which PR #198 adds, lists this problem as R5.

## 4. Other state that persists between calls

- BOSS core has no table of named handles and no global table of names. `Symbol::getName()`
  returns a value. chibi interns symbols per context (section 3).
- The C API returns pointers that the caller frees with the matching `free*` function:
  `BOSSExpression*`, `BOSSSymbol*`, and `char*` strings that `BOSS.cpp` allocates with `strdup`.
  BOSS core keeps no pointer to these objects.
- Between calls to `BOSSEvaluate`, the only state that BOSS core keeps is the `engine` object and
  its members (section 1).

---

## 5. Dispatch contract and locking

The comment at the top of `Source/BootstrapEngine.hpp` states the full contract. In short:

- **Engines.** Every engine's `evaluate` and `reset` must be reentrant. `BootstrapEngine` may call
  the same engine from several threads at once and never makes one dispatch wait for another.
- **Locking.** `engineStateMutex` is a `boss::concurrency::SharedMutex`, the wrapper from
  `Source/ThreadSafety.hpp` that declares Clang's capability attributes, so that
  `-Wthread-safety` can check every access to a `BOSS_GUARDED_BY` member. A root evaluation
  copies the engine pipeline and looks up engine functions under the shared lock, and loads a
  missing library under the exclusive lock (section 2). `BootstrapEngine` never holds
  `engineStateMutex` during a call into an engine.
- **Reconfiguration.** `SetDefaultEnginePipeline` and `ResetEngines` take the exclusive lock and
  call `beginReconfiguration`, which throws if `evaluationsInFlight` is not 0 and otherwise sets
  `reconfigurationInProgress`. `BootstrapEngine` increments `evaluationsInFlight` under the shared
  lock, so while a reconfiguration holds the exclusive lock, no dispatch can start. The
  reconfiguration then calls `LibraryCache::detachAll`, releases the lock and calls
  `unloadLibraries`. While `reconfigurationInProgress` is set, `InFlightGuard` refuses every new
  dispatch, because a dispatch in that interval could load an engine again and call it while
  `unloadLibraries` is still running that engine's `reset`. When the reconfiguration returns or
  throws, the `ReconfigurationEnd` object calls `finishReconfiguration`, which clears
  `reconfigurationInProgress`.
- **Destruction.** A destructor cannot refuse to run. If a dispatch is in flight when a
  `BootstrapEngine` is destroyed, `~LibraryCache` calls `tearDownOnceNoDispatchIsInFlight`, which
  appends the libraries to `librariesAwaitingTeardown` and sets `reconfigurationInProgress`. The
  `InFlightGuard` destructor that decrements `evaluationsInFlight` to 0 then calls
  `finishReconfiguration`, which resets and unloads the queued libraries.
- **Engine threading policies.** Because every engine is required to be reentrant, BOSS core has
  no per-engine setting that would make dispatches to a non-reentrant engine wait for each other.
  Such a setting can be added if an engine that is not reentrant needs to be supported.
- **No lock order.** BOSS core uses a single mutex, so no lock order exists that a formal model
  would need to check.

**Static checks.** `Source/ThreadSafety.hpp` defines `SharedMutex`, `SharedLock`, `UniqueLock` and
the `BOSS_GUARDED_BY` macro with Clang's thread-safety attributes. With Clang, `CMakeLists.txt`
compiles with `-Wthread-safety -Werror=thread-safety`, so the build fails on any access to
`defaultEnginePipeline` or `libraries` that does not hold `engineStateMutex`. With other
compilers the macros expand to nothing. During development, this analysis rejected a write to
the pipeline in `SetDefaultEnginePipeline` that happened outside the lock; that write now
happens under the exclusive lock.

**Debug-build check for a shared chibi context.** `ConcurrencyTripwire` in
`Source/ThreadSafety.hpp` records, for each chibi context, the thread that is evaluating in that
context and how deeply that thread has nested its evaluations. If a second thread starts an
evaluation in a context while another thread is evaluating in it, `ConcurrencyTripwire` prints
a message and calls `std::abort()`, so that a shared context crashes the program immediately
instead of corrupting memory. A nested evaluation on the same thread increases the depth and
does not abort. Builds that define `NDEBUG` replace `ConcurrencyTripwire` with an empty class.
PR #198 constructs a `ConcurrencyTripwire` at the start of `evaluate_expression`.

**Stress-test results.** PR #198 adds `Tests/Concurrency/stress.cpp`, the
`boss_concurrency_stress` target built with ThreadSanitizer, and
`Tests/Concurrency/known-races.md`, which numbers the races R1 to R5. On Apple clang 21 on arm64:

- **One chibi context per thread, expressions that dispatch to no engine:** more than 8000
  evaluations, no race reported. This result agrees with section 3.
- **One chibi context for all threads, expressions that dispatch to no engine:**
  ThreadSanitizer reports a race on the context's heap, and the program then crashes (R3). No
  two threads may share a context.
- **Engine pipeline (R1):** without the locking from PR #195, ThreadSanitizer reports the race on
  `defaultEnginePipeline` in the `engine` object. The harness shows the race only with
  `--direct`, which calls `BOSSEvaluate` without chibi. Through chibi, parsing and garbage
  collection take so much longer than the access to the pipeline that the accesses of different
  threads rarely overlap, so a clean run through chibi does not show that the race is absent.
  With the locking from PR #195, `--direct --suite pipeline` reports no race.
- **Library cache:** the harness can reach the race on `libraries` only when `--engine` names an
  engine library to load. The race is predicted from the code and not yet reproduced.
- **First chibi initialization (R4):** by default the harness creates one context before it starts
  the worker threads. With `--no-warmup`, the worker threads create their first contexts
  concurrently. With the `std::call_once` removed and chibi compiled with `-fsanitize=thread`,
  ThreadSanitizer reported the race on `sexp_initialized_p` in 6 of 6 runs on Linux.
- **Shared context with `ConcurrencyTripwire`:** in a Debug build, `--mode shared` aborts with the
  `ConcurrencyTripwire` message, and `--mode per-thread` does not.

## Where to find each mechanism

| Looking for | Where |
|---|---|
| The single `BootstrapEngine` | `BOSS.cpp`, `static boss::engines::BootstrapEngine engine` in `BOSSEvaluate` |
| The mutex for engine state | `BootstrapEngine.hpp`, `engineStateMutex` |
| Changes to the engine pipeline | `BootstrapEngine.hpp`, `SetDefaultEnginePipeline` and `ResetEngines` |
| Refusal of reconfiguration during a dispatch | `BootstrapEngine.hpp`, `evaluationsInFlight`, `reconfigurationInProgress`, `InFlightGuard`, `beginReconfiguration`, `ReconfigurationEnd` |
| Loading an engine on first use | `BootstrapEngine.hpp`, `resolveEvaluateFunction` and `LibraryCache::loadOrGet` |
| Unloading engines | `BootstrapEngine.hpp`, `LibraryCache::detachAll` and `unloadLibraries` |
| Thread-safety annotations and lock types | `Source/ThreadSafety.hpp`, `SharedMutex`, `SharedLock`, `UniqueLock`, `BOSS_GUARDED_BY` |
| Check for a shared chibi context | `Source/ThreadSafety.hpp`, `ConcurrencyTripwire` |
| Compiler flags for the thread-safety analysis | `CMakeLists.txt`, `-Wthread-safety -Werror=thread-safety` (Clang only) |
| Call from chibi into BOSS core | `Shims/chibi-bindings.stub`, the `BOSSEvaluate` binding; `ExpressionParser.hpp`, the `boss-eval` macro |
| Creation and destruction of chibi contexts | `ExpressionParser.hpp`, `initialize_boss_context` and `~BossContextGuard` |
| Standard-stream fix (R5) | `ExpressionParser.hpp`, the `sexp_load_standard_ports` call in `initialize_boss_context` |
| chibi's one-time initialization flag | chibi `sexp.c:22` and `:4025` |
| chibi's global heap and symbol flags | chibi `features.h:602-616`, which depend on `:507,527` |
