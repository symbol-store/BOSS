# Implementing a BOSS Loading Engine

This document is for someone building a BOSS engine whose primary job is to bring external data into the BOSS expression world: a file-format reader, a database connector, an API client. It uses FITSDKEngine — a wrapper around the Garmin FIT SDK — as the worked example throughout. Read the general [`EngineImplementationGuide.md`](EngineImplementationGuide.md) first for the contract a BOSS engine must honour; this guide focuses on what is different when your engine is mostly an ingest path.

For per-operator semantics in compute engines, see [`OperatorCatalog.md`](OperatorCatalog.md). A loading engine's operator surface is much narrower — typically one `Load` plus `GetEngineDescription` — and is documented inside the engine binary itself (see §9).

---

## 1. What a loading engine is

A loading engine answers a single question per call: given a `(Load path …)` expression, produce a BOSS `(Table …)` of the data on the other side. There is no query planner, no relational algebra, no compute-kernel dispatch — only a parser, a row builder, and a translation step.

Examples:

- Garmin FIT files → typed message tables (FITSDKEngine).
- CSV / TSV / JSON readers.
- A REST API client that fetches a paginated resource.
- A SQLite or Postgres bridge that runs a fixed `SELECT` and returns rows.

The shape is so common that BOSS pipelines typically chain several loading engines (one per format) ahead of a single compute engine. This guide describes the conventions that make that chain work.

### Loading vs. compute engines

| Aspect | Compute engine (ArrowComputeEngine) | Loading engine (FITSDKEngine) |
|---|---|---|
| Returns | Handle into an intermediate registry, materialized on demand | A fully materialized `(Table …)` expression |
| Registry | Required — holds plan fragments across handler calls | Not needed — each call is self-contained |
| Garbage collection | Per-call eviction of unreferenced intermediates | None |
| Operator vocabulary | Wide (Filter, Project, GroupBy, …) | Narrow (`Load` plus introspection) |
| Pattern arity | One handler per operator | Often *multiple* handlers per operator — overloaded by arity |
| Failure mode | Error string from a handler | Same |
| Pipeline position | Last in chain (consumes Tables) | First in chain (produces Tables) |
| Co-operates with peers? | Single engine per pipeline is usually enough | Must co-operate with *other loaders* (see §4) |

If your engine overlaps both (e.g. an Arrow IPC reader that also exposes Project / Filter pushdown), implement the loader side first using the patterns here, then add the compute side from the compute guide.

---

## 2. The plugin contract

Identical to compute engines:

```cpp
extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
  return new BOSSExpression{.delegate = evaluate(std::move(e->delegate))};
}
```

A loading engine doesn't need a separate `convertResult` step — the handler already returns a `(Table …)` expression, which is what the user sees. The C-ABI translation is a direct move.

There is no garbage collection because there are no intermediates. Each `evaluate` call is independent. That single difference removes most of the boilerplate the compute guide spends on (intermediates map, `Name` / `ByName`, eviction set, handle generation).

---

## 3. Project skeleton

Same `CMakeLists.txt` template as the compute guide, but with the third-party SDK fetched as a second `ExternalProject_Add`:

```cmake
ExternalProject_Add(FITSDK
  GIT_REPOSITORY ${FITSDK_SOURCE_REPOSITORY}
  GIT_TAG main
  INSTALL_COMMAND "")

target_include_directories(FITSDKEngine SYSTEM PUBLIC
  ${PROJECT_BINARY_DIR}/FITSDK-prefix/src/FITSDK/src)
add_dependencies(FITSDKEngine FITSDK BOSS)
```

Headers in your source file include both BOSS and the SDK:

```cpp
#include <BOSS.hpp>
#include <Expression.hpp>
#include <ExpressionUtilities.hpp>
#include <Utilities.hpp>

#include "fit_decode.hpp"
#include "fit_mesg_listener.hpp"
// …

using namespace boss::utilities::experimental;
using boss::ComplexExpression;
using boss::Expression;
using boss::ExpressionArguments;
using boss::Symbol;
using boss::utilities::operator""_;
```

FITSDKEngine fits in one source file (~700 lines including a long `GetEngineDescription` block). Keep yours small until an operator's implementation is genuinely large.

---

## 4. Path-driven dispatch — the central pattern

A loading engine in a multi-engine pipeline cannot simply "match Load and do its thing". Other loading engines also match `Load`. The dispatch must be **value-aware**: examine the path string, decide whether this format owns the data, and either load, partially load, or defer.

FITSDKEngine classifies an input path into three outcomes:

```cpp
enum class LoadDisposition { LoadOnly, Mixed, Defer };
struct LoadPlan {
  LoadDisposition disposition;
  std::vector<std::filesystem::path> ownedFiles;  // for FIT: the .fit entries
};

static LoadPlan planLoad(std::string const& path);
```

- **LoadOnly** — every input element belongs to this engine (a single `.fit` file, or a directory whose every entry is `.fit`). Read them all into a Table.
- **Mixed** — the input is a directory whose contents are partially ours and partially not. Read the `.fit` subset and wrap the result in `(Union (Load <path>) (Table …))` so the *next* engine in the pipeline picks up the remainder.
- **Defer** — none of the input belongs to us (a non-`.fit` file, or a directory with no `.fit` files). Return the original `(Load …)` expression unchanged for the next engine to handle.

### The handler shape

```cpp
< "Load"_(Any_, Symbol_, AnySequence_) >= Recurse(evaluate) >
    [](auto, auto dynamics, auto) -> Expression {
        auto const& path = std::get<std::string>(dynamics.at(0));
        auto plan = planLoad(path);
        if(plan.disposition == LoadDisposition::Defer)
            return "Load"_(std::move(dynamics));

        // … decode plan.ownedFiles into 'columns' …

        auto table = "Table"_(std::move(columns));
        if(plan.disposition == LoadDisposition::Mixed)
            return wrapForMixedDirectory(std::string(path), std::move(table));
        return std::move(table);
    }
```

The Defer branch returns the *original* `dynamics` — the next engine sees the same expression the user wrote, with no information lost.

The Mixed branch helper:

```cpp
static Expression wrapForMixedDirectory(std::string path, Expression fitTable) {
    return "Union"_("Load"_(std::move(path)), std::move(fitTable));
}
```

The inner `(Load <path>)` re-enters the dispatch chain at the *next* engine in the pipeline — your engine will already have returned for this call, so it is not re-matched against this handler.

### Why classify before consuming dynamics

Flag parsing (§7) moves entries out of `dynamics`. The Defer branch must run *before* that, because it needs the original arguments intact to hand back. Put the classification — and the Defer early return — first, then consume flags and other args:

```cpp
auto const& path = std::get<std::string>(dynamics.at(0));
auto plan = planLoad(path);
if(plan.disposition == LoadDisposition::Defer)
    return "Load"_(std::move(dynamics));

// Safe to consume dynamics from here on.
auto const& msgType = std::get<Symbol>(dynamics.at(1)).getName();
auto flags = parseFlagsFrom(dynamics, 2);
auto filePaths = std::move(plan.ownedFiles);
```

Every `Load` overload (see §5) follows this preamble.

### Designing the classifier

A good `planLoad` is small (~30 lines), pure, and answers in three branches:

1. Path is a directory → walk entries (single level), partition into "ours" and "other", choose disposition by whether the "other" bucket is empty.
2. Path is a single file we own → `LoadOnly` with one entry.
3. Anything else (non-matching file, broken path, symlink loop) → `Defer`.

Use the `std::error_code` overloads of `is_directory` and `directory_iterator` so that missing or unreadable paths fall through to Defer rather than throwing. Whether to recurse into subdirectories is an engine choice — FITSDKEngine stays single-level, matching how users organize FIT exports.

### What goes in `(Load path)` when you re-emit it

The inner `Load` in a Mixed wrap intentionally drops the original `msgtype` and flags. Reasons:

- The msgtype is FIT-specific; a downstream CSV or JSON loader has no notion of `session` or `record`.
- The flags are also engine-specific.

If your engine knows the next loader in the chain understands a particular flag, pass it through. Otherwise drop, keep the deferred call simple, and let the downstream engine apply its own defaults.

---

## 5. Operator overloads — arity-based dispatch

A loading engine often exposes two or three variants of `Load` for the same data source: a per-record read, a per-file summary read, a metadata-only read. BOSS pattern matching lets you put each variant in its own handler, dispatched on the structural shape of the arguments:

```cpp
< "Load"_(Any_, Symbol_, AnySequence_) >= Recurse(evaluate) > [](…) { /* row table */ }
< "Load"_(Any_,           AnySequence_) >= Recurse(evaluate) > [](…) { /* summary */ }
```

- The first matches `(Load path msgtype [flags …])` — a fully-typed message-stream load.
- The second matches `(Load path [flags …])` — same path, summary (one row per file).
- More specific patterns must come first; the chain stops at the first match.

This pattern keeps each handler narrowly scoped — no internal `if(dynamics.size() == 2)` switching, no shared error paths. Every overload gets its own classifier preamble, its own reader state, its own column builders.

### Choose which overload the deferred load targets

The inner `(Load <path>)` emitted from a Mixed wrap has one argument. Under the chain above, that matches the *summary* overload, not the row-table overload. For FITSDKEngine that's fine — the user gets a per-file summary from the downstream loader, which is the most reasonable fallback. If your engine wants the deferred load to target a specific overload, include the discriminating argument in the re-emitted `Load`.

---

## 6. Column builders and ragged rows

Real data is heterogeneous: rows of the same message type may have different field sets, and some fields are valid only on some rows. A loading engine builds *ragged* columns, then pads them with NULL to align before emitting.

The pattern is a small `ColumnBuilder`:

```cpp
struct ColumnBuilder {
    boss::expressions::ExpressionArguments values;
    size_t committedRows = 0;

    template<typename T>
    void add(T&& value) {
        values.emplace_back(std::forward<T>(value));
        ++committedRows;
    }

    boss::expressions::ExpressionSpanArguments build() &&;  // collapse to typed spans
};

struct MessageTable {
    std::unordered_map<std::string, ColumnBuilder> columns;
    size_t rowCount = 0;
};
```

Two invariants make this work:

1. **Pad before write.** Before adding a value to a column on row `N`, fill the column with NULLs up to `committedRows == N`. That way a column that first appears on row 50 silently gets 50 NULLs prepended.
2. **Pad after row.** After processing every field of a row, increment `rowCount` and pad any column that didn't receive a value on this row. This keeps all columns aligned at the row boundary.

`NULL` is represented as `Symbol("NULL")` — the same "named-null" idiom the Arrow engine uses on the way *in*. The user-facing wire shape is symmetric.

### Build spans, not dynamic arguments, for large columns

`ColumnBuilder::build()` collapses the accumulated values into typed `Span<T>`s by inspecting the variant alternatives. Span arguments are cheaper for both BOSS and downstream engines to walk than long lists of variant-tagged dynamic arguments. For columns up to a few hundred rows the difference doesn't matter; for thousands of rows it is the difference between a usable result and an unprintable one.

---

## 7. Symbolic parser flags

Loading engines have many optional knobs: scale-and-offset application, sub-field expansion, CRC validation, schema-evolution toggles. Expose them as **symbolic flags** at the end of `Load`:

```scheme
(Load "/path" session (apply_scale_and_offset 0) (merge_heart_rates 1))
```

Each flag is a one-argument ComplexExpression whose head is the flag name and whose argument is 0 or 1. Missing flags use defaults from a struct:

```cpp
struct ParseFlags {
    bool apply_scale_and_offset = true;
    bool expand_components = true;
    bool expand_sub_fields = true;
    bool convert_datetimes_to_dates = true;
    bool merge_heart_rates = false;
    bool enable_crc_check = true;
};

static ParseFlags parseFlagsFrom(ExpressionArguments& dynamics, size_t startIndex) {
    ParseFlags flags;
    for(size_t i = startIndex; i < dynamics.size(); ++i) {
        auto* flagExpr = std::get_if<ComplexExpression>(&dynamics[i]);
        if(!flagExpr) continue;
        auto [flagHead, _, flagArgs, __] = std::move(*flagExpr).decompose();
        bool value = true;
        if(!flagArgs.empty()) {
            if(auto* iv = std::get_if<int64_t>(&flagArgs[0])) value = (*iv != 0);
            else if(auto* dv = std::get_if<double>(&flagArgs[0])) value = (*dv != 0.0);
        }
        auto const& name = flagHead.getName();
        if(name == "apply_scale_and_offset") flags.apply_scale_and_offset = value;
        else if(name == "expand_components") flags.expand_components = value;
        // …
    }
    return flags;
}
```

Why symbolic flags rather than positional booleans:

- **Forward-compatible.** Adding a flag does not break existing call sites.
- **Self-documenting.** `(merge_heart_rates 1)` reads better than `(Load p s 1 1 0 1 1)`.
- **Discoverable.** `GetEngineDescription` (§9) can enumerate each flag and its default.

`parseFlagsFrom` moves the matched ComplexExpressions out of `dynamics`. That is why the Defer branch in §4 must run before any call to `parseFlagsFrom`.

---

## 8. Returning results

Three shapes only:

1. **A `(Table …)` expression** — the happy path.
2. **An error string** — prefix with the operator name and a colon: `"Load::error: cannot open file: …"`. The REPL prints these as ordinary values; the user sees them.
3. **A `(Union (Load path) (Table …))` expression** — partial coverage, as described in §4.

A loading engine does not return handles. Every result is fully materialized at the point the handler returns.

### Discouraging unbounded loads

A `Load` that walks a directory of thousands of files can produce a result that exceeds BOSS's transport limits. The convention is to *expose* a summary form that returns one row per file — the second `Load` overload in FITSDKEngine — and document in `GetEngineDescription` (§9) that callers should aggregate before returning a full row-stream load. Phrase it as a constraint, not a suggestion: LLM callers will treat it as one.

---

## 9. Engine description

Same convention as compute engines: a `GetEngineDescription` operator returning a multi-line string literal. For a loading engine, the string should answer:

1. What format does this engine read?
2. What does each `Load` overload return?
3. What flags exist, what are their defaults?
4. What path conventions apply (absolute vs relative, glob support, Unicode caveats)?
5. What does the engine do when handed a path it doesn't own (defer? Union? error?)?

Keep the text in sync with this document and with the implementation. An LLM caller will read this string to decide what queries to send — drift between description and behaviour is a real cost.

FITSDKEngine's description is at the bottom of `Source/FITSDKEngine.cpp` (in the [FITSDKEngine](https://github.com/symbol-store/FITSDKEngine) repository, not this one). It includes worked-example queries, a hard-won Unicode-in-filenames hazard note (Apple-Watch exports embed U+00A0 where they look like spaces), and a section on mixed-content directory dispatch. Use it as a template.

---

## 10. Testing

Same harness as compute engines: a Chibi Scheme REPL test file invoked via `build/deps/bin/boss test.scm`.

Loading-engine-specific test cases to write:

- **Round-trip per format variant.** Load a known fixture, compare the result to a hand-written `(Table …)`.
- **Defer behaviour.** Invoke `(Load "/path/that/is/not/ours.txt")` and assert the result is the un-evaluated `(Load …)` expression (or a downstream engine's result if the test pipeline has one).
- **Mixed-directory behaviour.** Stage a directory with one `.fit` and one non-`.fit` file; assert the result is `(Union (Load <dir>) (Table …))`.
- **Flag effects.** Load the same fixture twice with a flag flipped; assert the columns or values differ as documented.
- **Error path.** Load a corrupt fixture; assert the result starts with `"Load::error:"`.
- **Empty fixture.** An empty `.fit` directory should *defer*, not error.

Ship reference fixtures in the repo at known paths so tests are reproducible. FITSDKEngine ships `example.fit` and `FunctionalStrengthTraining.fit` at the repo root for this purpose.

---

## 11. Lessons from FITSDKEngine

### Classify the path first, then commit.

Every handler starts with `planLoad(path)` and a Defer-or-continue check before touching the rest of `dynamics`. This is the only way to be a good citizen in a multi-loader pipeline.

### Don't pre-walk subdirectories.

A `Load` on a directory should consider only that directory's *direct* children. If users want recursive loads, they will write a glob or compose loaders. Engines that silently walk subtrees become impossible to reason about when a hidden subdirectory contains incompatible data.

### `Symbol("NULL")` is the wire format for missing values.

Consume them on input, emit them on output. Don't invent a parallel null sentinel.

### Treat the description text as documentation owned by the engine binary.

The user can ask the running engine what it supports — they don't need to read your repo. Keep examples in `GetEngineDescription` runnable, and update them whenever you change a flag default. A note about a Unicode hazard in filenames is worth more in the binary than in a wiki nobody finds.

### Don't throw across the C ABI.

Same as compute engines. Catch SDK exceptions at the per-file boundary, translate to `"Load::error: …"` strings. A C++ exception leaving `extern "C" evaluate` is undefined behaviour and will at best crash the REPL.

### Don't bloat the engine with a query planner.

If your callers want filtering or aggregation on top of a load, let a compute engine downstream do it. Loading engines stay narrow. The temptation to add a `LoadFiltered` is real; resist it — `(Filter (Load …) …)` composes through the pipeline cleanly.

### Identifier spelling.

Recommended: avoid abbreviations in identifiers. `columnName`, not `colName`; `arguments`, not `args`. Single-file engines are read end-to-end; full words make the read tractable.

### One file, one namespace, ~600–700 lines.

FITSDKEngine fits in one `Source/FITSDKEngine.cpp`. The discipline pays off when an LLM agent reads the source — there are no hidden corners. Split only when an operator's implementation is genuinely large.

---

## 12. References

> The FITSDKEngine sources below live in the [FITSDKEngine](https://github.com/symbol-store/FITSDKEngine) repository, not in
> this checkout.

| File | Contents |
|---|---|
| `Source/FITSDKEngine.cpp` | Whole engine — read end-to-end (~700 lines) |
| `build/deps/include/BOSS.{h,hpp}` | C ABI and C++ delegate wrapper |
| `build/deps/include/Expression.hpp` | Atoms, spans, ComplexExpression |
| `build/deps/include/ExpressionUtilities.hpp` | Pattern-matching DSL |
| [`EngineImplementationGuide.md`](EngineImplementationGuide.md) | Pattern dispatch, registries, type bridging — general engine contract |
| [`OperatorCatalog.md`](OperatorCatalog.md) | Per-operator semantics for relational engines |

---

## Appendix: minimal viable loading engine

```cpp
#include <BOSS.hpp>
#include <Expression.hpp>
#include <ExpressionUtilities.hpp>
#include <Utilities.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace boss::utilities::experimental;
using boss::ComplexExpression;
using boss::Expression;
using boss::ExpressionArguments;
using boss::Symbol;
using boss::utilities::operator""_;

namespace {
enum class LoadDisposition { LoadOnly, Mixed, Defer };
struct LoadPlan {
    LoadDisposition disposition;
    std::vector<std::filesystem::path> files;
};
} // namespace

static LoadPlan planLoad(std::string const& path, std::string const& extension) {
    LoadPlan plan{LoadDisposition::Defer, {}};
    std::error_code ec;
    std::filesystem::path p(path);
    if(std::filesystem::is_directory(p, ec)) {
        bool other = false;
        for(auto const& entry : std::filesystem::directory_iterator(p, ec)) {
            if(entry.path().extension() == extension) plan.files.push_back(entry.path());
            else other = true;
        }
        if(plan.files.empty()) return plan;
        plan.disposition = other ? LoadDisposition::Mixed : LoadDisposition::LoadOnly;
        return plan;
    }
    if(p.extension() == extension) {
        plan.files.push_back(p);
        plan.disposition = LoadDisposition::LoadOnly;
    }
    return plan;
}

static Expression evaluate(Expression&& e) {
    using sentinel::Any_;
    using sentinel::AnySequence_;
    return std::move(e)
        < "Load"_(Any_, AnySequence_) >= Recurse(evaluate) >
          [](auto, auto dynamics, auto) -> Expression {
              auto const& path = std::get<std::string>(dynamics.at(0));
              auto plan = planLoad(path, ".txt");
              if(plan.disposition == LoadDisposition::Defer)
                  return "Load"_(std::move(dynamics));

              auto columns = ExpressionArguments{};
              // … read plan.files, fill columns …
              auto table = "Table"_(std::move(columns));

              if(plan.disposition != LoadDisposition::Mixed) return std::move(table);
              return "Union"_("Load"_(std::string(path)), std::move(table));
          }
        < "GetEngineDescription"_() >= [](auto, auto, auto) -> Expression {
              return std::string("(Load path) — loads .txt files; defers others.");
          }
        < Any_ >= Recurse(evaluate);
}

extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
    return new BOSSExpression{.delegate = evaluate(std::move(e->delegate))};
}
```

That is a complete loading engine: it classifies, defers, optionally wraps, and self-describes. Specialize the file reader and the column builders for your format.
