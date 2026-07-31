# Tests/Concurrency — BOSS core thread-safety harness

Phase 2 of the thread-safety hardening plan. See `docs/threading-audit.md` for the inventory
this exercises and `known-races.md` for the TSan triage log.

## What's here

| File | Purpose |
|------|---------|
| `stress.cpp` | The `boss_concurrency_stress` harness: N threads × M iterations of a fixed expression suite, in shared-context or per-thread-context mode. |
| `known-races.md` | Triage log for every TSan report (classification + in-scope flag + status). |
| `tsan_suppressions.txt` | Opt-in suppressions, each tied to a `known-races.md` ID. |

## Build

ThreadSanitizer conflicts with the ASan-based `Sanitize` build type, so build in a separate,
non-`Sanitize` dir with the REPL (chibi) enabled:

```sh
mkdir -p build-tsan && cd build-tsan
CXX=clang++ CC=clang cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-std=c++17" ..
cmake --build . --target boss_concurrency_stress -- -j"$(nproc)"
```

The target is `EXCLUDE_FROM_ALL`; build it explicitly. `-DBOSS_CONCURRENCY_STRESS=OFF`
disables it entirely.

## Run

```sh
# clean gate — the configuration we require to be race-free:
TSAN_OPTIONS="halt_on_error=1 abort_on_error=1" \
  ./boss_concurrency_stress --mode per-thread --suite pure --threads 8 --iters 2000

# known-unsafe baseline — should report a race (confirms TSan catches a shared ctx):
TSAN_OPTIONS="halt_on_error=0" \
  ./boss_concurrency_stress --mode shared --suite pure --threads 8 --iters 500

# dispatch + reconfig gate — race-free since Phase 4 (was the R1 reproducer).
# --direct bypasses chibi: the chibi path buries the dispatch race under parse/GC work, so
# direct dispatch is the reliable probe (confirmed race on `BOSSEvaluate::engine` pre-Phase 4).
TSAN_OPTIONS="halt_on_error=1 abort_on_error=1" \
  ./boss_concurrency_stress --direct --suite pipeline --threads 8 --iters 5000
```

Options: `--threads N`, `--iters M`, `--mode shared|per-thread`, `--direct` (bypass chibi and
hammer `boss::evaluate()` dispatch — best at surfacing dispatch races), `--suite
pure|pipeline|mixed`, `--engine PATH` (exercise the dlopen registry race through a real
engine), `--no-warmup` (expose the chibi init-time race), `--seed S`, `--quiet`. `--help` lists
them all.

## Debug tripwire (Phase 5)

In a **Debug** build (`-DCMAKE_BUILD_TYPE=Debug`, i.e. `NDEBUG` unset), the `ConcurrencyTripwire`
in `Source/ThreadSafety.hpp` is active: `--mode shared` aborts immediately with a diagnostic
naming the shared context, instead of relying on TSan to catch the corruption. In
`RelWithDebInfo`/`Release` the tripwire compiles away and TSan does the catching — so build
Debug to see the tripwire, RelWithDebInfo to run the TSan gates.

## CI

The `concurrency-stress` job in `.github/workflows/ccpp.yml` runs two hard gates — the
per-thread+pure clean gate and the `--direct` pipeline dispatch+reconfig gate (race-free since
Phase 4) — plus the shared-context baseline as informational (it is expected to race). The
Clang Thread Safety Analysis gate (`-Werror=thread-safety`) is enforced by the normal `build`
job, since the annotations live in the core headers it compiles.
