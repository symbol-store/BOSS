# Implementing a BOSS Engine

This document is written for a Claude instance (or human developer) about to implement a new engine plugin for the BOSS framework. It uses ArrowComputeEngine as the worked example throughout. It is descriptive, not prescriptive: it documents what ArrowComputeEngine does and why, so you can copy the patterns that fit your engine and diverge where they do not.

For per-operator semantics, see [`OperatorCatalog.md`](OperatorCatalog.md).

---

## 1. What a BOSS engine is

BOSS is an expression-oriented data management framework. The user writes expressions like `(Filter (Load "data.csv") (Greater age 18))` at a Scheme REPL, in a script, or via the C ABI. BOSS parses the expression, hands it to the engine pipeline, and prints the result.

An engine is a shared library that exposes one symbol — `evaluate` — and receives whole expressions to interpret. BOSS does not enforce a query language, a type system, or an operator vocabulary: it gives the engine a parsed expression tree and trusts the engine to return another expression.

Engines can therefore be:

- **Thin wrappers** around an existing query engine (Acero, DuckDB, Velox, a Substrait-consuming runtime).
- **Purpose-built** to implement a narrow algorithm — a graph traversal, an ML inference pipeline, a single-operator profiling harness.
- **Compositional**: BOSS supports a pipeline of engines, each given the chance to handle (or pass through) an expression.

ArrowComputeEngine is a thin wrapper around Apache Arrow's Acero query engine, plus a few escape-hatch operators (`Cumulate`, `Pairwise`) implemented via Arrow's scalar compute kernels because Acero does not support stateful row-ordered operators.

---

## 2. Choose your engine shape

Before you start, decide which of the two shapes below your engine most resembles. The skeleton is identical; the handler bodies differ sharply.

### 2a. Wrapper engines (delegate to an existing planner)

You translate BOSS expressions into your planner's plan-node types (`Declaration`, `LogicalOperator`, `PlanNode`, a Substrait fragment, …) and execute on demand.

- Most operator handlers translate one BOSS expression into one planner node and store it in an in-memory registry under a numeric handle.
- Execution happens lazily — when the user materializes the final result, or hits a `ToStatus` / `Materialize` boundary.
- Type bridging is done at the boundary (data is converted once on `Load`, the final result is converted back to a BOSS expression at the outermost call).
- **See:** every `> [](auto, auto dynamics, auto) { ... }` body in `Source/ArrowComputeEngine.cpp` is one of these wrappers.

### 2b. Purpose-built engines (implement operators yourself)

You compute results in C++ against your own data structures.

- Each handler computes immediately and returns either a fresh value or a handle into your registry.
- You may not even need a registry — if every operator returns a complete result, just convert it inline.
- You are free to use a much narrower operator vocabulary than ArrowComputeEngine's relational set.
- **See:** `Cumulate` and `Pairwise` in `ArrowComputeEngine.cpp` are mini-examples — they call Arrow compute kernels directly rather than building an Acero plan, then re-wrap the result as a `table_source` node so downstream operators see a uniform input shape.

The rest of this guide is structured around the wrapper case, with notes for the purpose-built case where they differ.

---

## 3. The plugin contract

BOSS loads your shared library via `dlopen` and looks up exactly one symbol:

```cpp
extern "C" BOSSExpression* evaluate(BOSSExpression* e);
```

This is the only required entry point. Every other interaction with BOSS happens through types defined in `BOSS.h`, `BOSS.hpp`, `Expression.hpp`, `ExpressionUtilities.hpp` (located under `Build/deps/include/` after a successful build).

### What BOSS guarantees you

- `e->delegate` is a fully-parsed `boss::Expression`, almost always rooted at a `ComplexExpression`.
- BOSS will `freeBOSSExpression` your return value when it is done with it.
- BOSS will not call `evaluate` concurrently on the same engine instance (single-threaded entry).

### What you must guarantee BOSS

- Return a `BOSSExpression*` allocated with `new BOSSExpression{…}`.
- Do not let C++ exceptions escape the `extern "C"` boundary. Catch them and convert to a string-valued result if they reach the top of `evaluate`.
- Do not assume your library state outlives a single call sequence. (In practice BOSS does not unload engines mid-session, but you should not rely on it.)

### The minimal `evaluate`

```cpp
extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
  return new BOSSExpression{
      .delegate = intermediates.convertResult(evaluate(std::move(e->delegate)))};
}
```

Two things happen here:

1. The internal `evaluate(boss::Expression&&)` function (described in §6) walks the expression, runs the dispatch chain, and returns either a handle (`int64_t`) or a literal expression.
2. `convertResult` turns a handle back into a full `Table(…)` expression for the user, or passes literals through unchanged.

ArrowComputeEngine's real `evaluate` additionally evicts unreferenced intermediates from its registry — see §7 (Lifetime / garbage collection).

---

## 4. Project skeleton

ArrowComputeEngine is one source file plus a CMake script. Aim for the same minimum surface unless you have a very large operator set.

### CMakeLists.txt

The build fetches BOSS via `ExternalProject_Add` and links the engine as a `MODULE` library (so `dlopen` works on Linux/macOS) or `SHARED` on Windows. Key fragments:

```cmake
ExternalProject_Add(BOSS
  GIT_REPOSITORY ${BOSS_SOURCE_REPOSITORY}
  GIT_TAG ${BOSS_SOURCE_BRANCH}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/deps ...)

if(MSVC) set(LibraryType SHARED) else() set(LibraryType MODULE) endif()
add_library(MyEngine ${LibraryType} Source/MyEngine.cpp)

set_property(TARGET MyEngine PROPERTY CXX_STANDARD 23)
target_include_directories(MyEngine SYSTEM PUBLIC ${PROJECT_BINARY_DIR}/deps/include)
add_dependencies(MyEngine BOSS)
```

If your engine wraps a third-party library, add a second `ExternalProject_Add` (or use `FetchContent` / `find_package`) and link its static or shared archives. ArrowComputeEngine's `CMakeLists.txt` is a good template to copy verbatim — it also defines a `Sanitize` build type with ASan/UBSan flags, which is invaluable when debugging dispatch-chain or std::variant misuse early on.

### Headers and namespaces

```cpp
#include <BOSS.hpp>
#include <Expression.hpp>
#include <ExpressionUtilities.hpp>
#include <Utilities.hpp>

using namespace boss::utilities::experimental;
using boss::ComplexExpression;
using boss::Expression;
using boss::Symbol;
using boss::utilities::operator""_;   // enables the "Foo"_ symbol literal
```

The pattern-matching DSL lives in `boss::utilities::experimental`; the operator overloads it relies on are picked up via ADL once that namespace is `using`-imported. Without the `using namespace`, the `<` / `>=` / `>` chain in §6 will not resolve.

---

## 5. Expressions — the data your engine consumes and emits

A `boss::Expression` is a tagged variant. Read `Expression.hpp` for the exact list; the high-level picture is:

```
Expression  =  bool | int8 | int32 | int64 | float | double | string | Symbol | ComplexExpression
```

A `ComplexExpression` has four parts:

- **Head**: a `Symbol` — the operator name (`Filter`, `Project`, …).
- **Static arguments**: a `std::tuple` of compile-time-typed args. Rare; you can almost always ignore them when consuming parsed expressions from BOSS.
- **Dynamic arguments**: a `std::vector<Expression>` — the parsed children. This is the slot you will read 95% of the time.
- **Span arguments**: a list of `Span<T>` — typed columnar slices, used for bulk literal data (e.g. the inline columns of `Table((A 1 2 3 4 5))`). The Arrow engine's `Table` handler shows the full traversal.

The literal operator `"Foo"_` constructs a `Symbol("Foo")`. It is also the basic builder for ComplexExpressions: `"Filter"_(table, predicate)` builds `Filter(table, predicate)`.

### Reading expressions

```cpp
auto const& ce = std::get<ComplexExpression>(someExpr);
ce.getHead().getName();             // "Filter"
auto const& dynamics = ce.getDynamicArguments();
auto const& spans    = ce.getSpanArguments();

std::visit(boss::utilities::overload(
    [](Symbol const& s)            { /* column ref or symbol literal */ },
    [](std::string const& s)       { /* string literal */ },
    [](int64_t v)                  { /* integer literal */ },
    [](ComplexExpression const& c) { /* nested expression */ },
    [](auto const&)                { /* other atoms */ }),
  dynamics.at(0));
```

### Building expressions

```cpp
Expression result = "Table"_("Columns"_, Symbol("A"), Symbol("B"));
// or assemble dynamically:
boss::ExpressionArguments args;
args.push_back(Symbol("A"));
args.push_back(int64_t{42});
Expression t = ComplexExpression("Table"_, std::move(args));
```

---

## 6. Pattern-matching dispatch — cookbook

ArrowComputeEngine's entire dispatch logic is one chained expression inside `static boss::Expression evaluate(boss::Expression&& e)`. The chain has the shape:

```cpp
return std::move(e)
  < "Foo"_(AnySequence_) >= Recurse(evaluate) > [](auto head, auto dynamics, auto spans) { … }
  < "Bar"_(Any_, Any_)   >= Recurse(evaluate) > [](auto head, auto dynamics, auto spans) { … }
  < "Baz"_(AnySequence_) >= Recurse(evaluate) > [](auto, auto dynamics, auto)            { … };
```

You do not need to understand the operator overloads. The cookbook below covers every shape used in the Arrow engine.

### Pattern templates

| Pattern                              | Matches                                                                  |
|--------------------------------------|--------------------------------------------------------------------------|
| `"Op"_(AnySequence_)`                | `Op(…)` with any number of arguments                                      |
| `"Op"_(Any_, Any_)`                  | `Op(arg1, arg2)` — exactly two arguments of any type                      |
| `"Op"_(Any_)`                        | `Op(arg)` — exactly one argument                                          |
| `"Op"_()`                            | `Op()` — no arguments                                                     |
| `"Op"_(Symbol_, String_, Integer_)`  | `Op(sym, str, int)` — typed single-arg sentinels                          |

The `Any_`, `AnySequence_`, `Symbol_`, `String_`, `Integer_` sentinels live in `boss::utilities::experimental::sentinel` (already in scope via the `using namespace` from §4).

### `Recurse(evaluate)`

When `>= Recurse(evaluate)` appears between the pattern and the lambda, BOSS walks the dynamic arguments of the matched expression and recursively evaluates each one **before** your lambda runs. In practice: by the time your handler runs, all sub-expressions are already evaluated — `dynamics.at(0)` is a handle (or a literal), not an `(Project …)` expression.

Omit `Recurse(evaluate)` when you want the raw sub-expression — e.g. inside a `Quote` operator, when you intend to walk the AST yourself for compilation purposes, or when implementing an introspection operator that should not evaluate its argument.

### Handler lambda signature

The lambda receives three arguments:

```cpp
[](auto head      /* Symbol */,
   auto dynamics  /* ExpressionArguments — i.e. std::vector<Expression> */,
   auto spans     /* SpanArguments */) -> boss::Expression { ... }
```

Most handlers ignore `head` (it is always the operator name you just matched) and `spans` (most operators receive `dynamics` only). Use `[](auto, auto dynamics, auto) { … }` to drop them — that is what every handler in `ArrowComputeEngine.cpp` does.

### Common handler shapes

**Wrap a sub-plan into the registry:**

```cpp
< "Filter"_(Any_, Any_) >= Recurse(evaluate) > [](auto, auto dynamics, auto) {
    auto columns = intermediates.columnNames(dynamics.at(0));
    return intermediates.put(Declaration::Sequence(
        {intermediates.at(dynamics.at(0)),
         {"filter", FilterNodeOptions(toComputeExpression(dynamics.at(1), columns))}}));
}
```

**Variadic args (`OrderBy`, `Project`, `GroupBy`):**

```cpp
< "Project"_(AnySequence_) >= Recurse(evaluate) > [](auto, auto dynamics, auto) {
    for(auto i = 1u; i < dynamics.size(); ++i) {
        // first arg is the table-handle; subsequent are projection specs
    }
    …
}
```

**Return a literal (no registry needed):**

```cpp
< "ToStatus"_(AnySequence_) >= Recurse(evaluate) > [](auto, auto dynamics, auto) {
    return DeclarationToStatus(intermediates.at(dynamics.at(0)), false).CodeAsString();
}
```

**Return a fixed string (introspection):**

```cpp
< "GetEngineDescription"_(AnySequence_) >= Recurse(evaluate) > [](auto, auto, auto) {
    return std::string(R"(… operator catalog text …)");
}
```

**Conditional re-write (no registry, no recursion):**

If you want to intercept an expression and rewrite it into another BOSS expression, return the new expression directly. Omitting `Recurse(evaluate)` keeps the children un-evaluated, which is useful for macro-style expansion.

### Fallback / pass-through

The dispatch chain stops at the first matching pattern. Anything that does not match falls out of the chain unchanged. To surface "unknown operator" errors, add a terminal pattern that matches all heads and inspects `head.getName()`, or compose with a downstream engine in the BOSS pipeline that handles unknown operators differently.

### One-time initialization

The dispatch chain runs every call. To initialize once per process, use a static local at the top of `evaluate`:

```cpp
static auto _ = arrow::compute::Initialize();
```

This is the idiom ArrowComputeEngine uses to ensure Arrow's compute registry is populated before the first kernel call. Use the same pattern to register your own kernels, open long-lived connections, or build static lookup tables.

---

## 7. Managing intermediate results

For wrapper engines, the dispatch handlers do not run the query — they build a plan. You need somewhere to keep the plan while the user composes more operators on top of it.

ArrowComputeEngine uses a single static struct:

```cpp
static struct {
  std::unordered_map<size_t, Declaration> intermediates;
  std::unordered_map<boss::Symbol, size_t> names;

  int64_t put(Declaration&& d) {
    auto id = generateID();
    intermediates[id] = std::move(d);
    return id;
  }
  Declaration const& at(boss::Expression const& key) { /* lookup or throw */ }
  // …
} intermediates;
```

Each handler stores its sub-plan and returns the integer handle as a `boss::Expression`. Downstream handlers fetch the parent's plan with `intermediates.at(dynamics.at(0))`.

### Handle types

The handle just needs to be a BOSS atom. `int64_t` is the natural choice because it can be passed through expressions cheaply and is unlikely to be confused with user data. Random IDs (rather than monotonic) make accidental collisions across pipeline runs detectable; ArrowComputeEngine uses a `default_random_engine` seeded from `std::random_device`.

### `Name` and `ByName` — long-lived references

When the user writes:

```scheme
(Name (Materialize (OrderBy …)) sorted)
…
(ByName sorted)
```

`Name` is the engine's hook to mark a handle as "do not garbage-collect this — the user has bound it". `ByName` retrieves it. ArrowComputeEngine implements this with a second map:

```cpp
boss::Expression name(boss::Expression&& key, boss::Symbol name) {
  names[name] = std::get<int64_t>(key);
  return std::move(key);
}
int64_t byName(boss::Symbol name) { return names.at(name); }
```

### Lifetime / garbage collection

At the end of every top-level `evaluate` call, ArrowComputeEngine evicts every intermediate that is not referenced from `names`:

```cpp
extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
  auto result = new BOSSExpression{
      .delegate = intermediates.convertResult(evaluate(std::move(e->delegate)))};

  auto live = std::set<size_t>();
  for(auto& [name, key] : intermediates.names) live.insert(key);
  std::erase_if(intermediates.intermediates,
                [&](auto& kv) { return !live.count(kv.first); });
  return result;
}
```

This is intentional: it keeps the registry from growing unboundedly across REPL invocations, while letting the user explicitly retain results across calls via `Name`. If your engine's intermediates are very cheap (small plan handles), you can skip the GC. If they own large in-memory data, you should keep this idiom.

---

## 8. Returning final results

A handle is only meaningful inside the engine. When the user sees the result of a `boss-eval`, they expect an actual `Table(…)` expression they can read. The `convertResult` function does that translation at the C-ABI boundary.

```cpp
boss::Expression convertResult(boss::Expression const& key) {
  if(!std::holds_alternative<int64_t>(key))
    return key.clone(boss::expressions::CloneReason::EVALUATE_CONST_EXPRESSION);

  // key is a handle — materialize the plan into an arrow::Table, walk columns,
  // emit ComplexExpression("Table"_, …) with one (ColumnName val val val …) child per column.
}
```

Cases your `convertResult` must handle:

1. **Handle** — look up the plan, run it, convert each column into a `(ColumnName val val val …)` ComplexExpression, return `Table(…)`.
2. **Literal scalar** (string, int, bool, double) — pass through unchanged.
3. **Symbol** — pass through (e.g. results of pure symbolic operations).
4. **Error string** — see below; no special handling required.

### Returning errors

Engines return errors by returning a `std::string` from inside a handler. `convertResult` does not need special handling: the string flows through as a literal and the user sees it printed in the REPL.

```cpp
auto maybeTable = compute::CallFunction(…);
if(!maybeTable.ok())
  return boss::Expression{maybeTable.status().ToStringWithoutContextLines()};
```

This is the engine's exception-equivalent: instead of throwing across the C ABI, propagate a string up the chain.

---

## 9. Type bridging

You map your library's types to BOSS atoms at the boundary in both directions.

### Inbound (BOSS → your library)

The `Table((col v1 v2 …) …)` operator is the bulk-input idiom. ArrowComputeEngine's handler for `Table` shows how to:

- Peek at the first non-symbol value to infer the column type.
- Use a typed builder (`Int64Builder`, `DoubleBuilder`, `StringBuilder`, `BooleanBuilder`).
- Treat `Symbol` values inside a typed column as nulls (the "named-null" idiom: `(A 1 NULL 3)` parses with `NULL` as a Symbol because it is not surrounded by quotes).
- Treat a column whose values are *all* symbols as a string column with a special `boss_type=symbol` Arrow field metadata, so that on the way out the same column is re-emitted as symbols rather than strings.

If your library has its own bulk-load mechanism, you may want a different entry operator (e.g. `LoadParquet`, `LoadFromMemory`, `ConnectTo`) that does not need this Table-construction dance.

### Outbound (your library → BOSS)

The `ColumnConverter` visitor in `ArrowComputeEngine.cpp` shows how to turn each scalar into a BOSS atom, falling back to a generic `(ArrowType …)` wrapper for types BOSS does not natively understand (decimals, dictionary-encoded values, list types, etc.). The principle is: convert what you can, wrap the rest in a typed ComplexExpression so the user can still see it.

When a column is logically symbols (the `boss_type=symbol` metadata is set), emit BOSS `Symbol` values, not strings — that is what round-trips through `(Table (col Sym1 Sym2 Sym3))`.

### Symbol-as-column-ref disambiguation

Symbols inside operator predicates are overloaded: a `Symbol("foo")` argument to `Equal` is a column reference if `foo` is in the current schema, and a string-valued literal otherwise:

```cpp
// inside toComputeExpression
[&columns](Symbol const& s) {
  return columns.count(s.getName())
      ? compute::field_ref(s.getName())
      : compute::literal(std::make_shared<arrow::StringScalar>(s.getName()));
}
```

This is what lets the user write `(Filter t (Equal sport Running))` without quoting `Running`. Replicate the pattern (or document a different rule) when designing your filter/project conventions.

### A small but useful detail: head-name aliasing

ArrowComputeEngine lowercases operator heads before looking them up in Arrow's compute function registry and applies a handful of aliases:

```cpp
static std::unordered_map<std::string, std::string> const aliases = {
    {"countall", "count_all"}, {"ifelse", "if_else"},
    {"lessequal", "less_equal"}, {"greaterequal", "greater_equal"},
    {"notequal", "not_equal"}, {"not", "invert"},
    {"avg", "mean"}, {"isvalid", "is_valid"},
};
```

This bridges BOSS's PascalCase convention to Arrow's snake_case. If your target library uses different naming, build the equivalent map at the toX-name boundary.

---

## 10. Self-description: `GetEngineDescription`

ArrowComputeEngine implements a `GetEngineDescription()` operator that returns a multi-line string listing every supported operator and its signature. The string is returned as a literal — no plan, no registry.

Why bother:

- The user can ask a running engine what it supports without leaving the REPL.
- A wrapper script or another engine in the pipeline can introspect capabilities programmatically.
- For LLM-driven query generation, the string can be pasted into a prompt to constrain output.

If you implement this convention, keep the text in sync with `OperatorCatalog.md` — see the bottom of `ArrowComputeEngine.cpp` for the source of truth this engine uses. Drift between the in-binary description and the markdown catalog is a real cost over time; an LLM agent maintaining the engine should always update both.

---

## 11. Testing

ArrowComputeEngine uses BOSS's bundled Chibi Scheme REPL as the test runner. Tests are `.scm` files invoked via `Build/deps/bin/boss <test-file.scm>`.

The harness pattern is:

```scheme
(import (chibi test))
(boss-eval (SetDefaultEnginePipeline "Build/libArrowComputeEngine.so"))

(test-group "Filter"
  (test "Filter: greater"
        '(Table (A 5))
        (boss-eval (Filter (Table (A 5 1 3)) (Greater A 4)))))
```

A `test` call evaluates the third form, compares it to the second using `equal?`, and reports pass/fail. The expected result is a quoted BOSS expression — exactly what the user would see printed.

### Test file structure

ArrowComputeEngine ships three families of tests:

- `Tests/repl-tests.scm` — operator-by-operator unit tests + TPC-H-style composition tests on inline data. Runs in seconds, requires no external files.
- `Tests/tpch-queries.scm` — query plans expressed as `define-syntax` macros, shared between the unit tests and the benchmarks.
- `Tests/tpch-bench.scm` / `Tests/tpch-sf10-correctness.scm` — runs the same queries against full TPC-H data (`TPCHData/`) and times them or checks expected output.

### Reference outputs

The `expected_results/` directory holds `*.stable.out` files, one per TPC-H query, against which the correctness test compares. Keep these files under version control; treat changes to them as deliberate.

### A minimum test suite for a new engine

Even a one-operator engine should have:

1. A `test-group` per operator covering the happy path, edge cases (empty input, nulls, type mismatches), and composition with other operators.
2. A round-trip test: `(Schema (Table …))` must produce the column names you expect.
3. A `GetEngineDescription` smoke test (`(string? (boss-eval (GetEngineDescription)))`).
4. A way to run the full suite without external data, even if some tests are gated behind data presence.

---

## 12. Lessons from ArrowComputeEngine

### The escape hatch

`Cumulate` and `Pairwise` exist because Acero does not natively support stateful, row-ordered operations. Rather than reject these operators, the engine bypasses the planner and calls Arrow's scalar compute API directly, then re-wraps the result as a `table_source` Declaration so downstream operators see a uniform shape.

The lesson: when your wrapped library cannot express something, drop down a level. As long as your handler returns a handle that fits into your registry, downstream handlers do not care how the plan was produced.

### `Recurse` ordering matters

`Recurse(evaluate)` must come *between* the pattern and the lambda (`pattern >= Recurse(evaluate) > lambda`). Forget it and your lambda receives un-evaluated child expressions (still parsed as `(Filter …)`, not `int64_t`). Put it in the wrong slot and the chain will not compile.

### Lambdas drop the args they don't use

The lambda signature is fixed: `(head, dynamics, spans)`. Use `auto` for all three and drop the names for the ones you ignore. This is why every handler in `ArrowComputeEngine.cpp` reads `[](auto, auto dynamics, auto)`.

### Don't throw across the C ABI

A C++ exception escaping `extern "C" evaluate` is undefined behaviour and will at best crash the REPL. The engine returns errors as `std::string` expressions; the REPL prints them as ordinary values. If you call into a library that throws, wrap the call in a try/catch at the boundary.

### Identifier spelling

Per the README convention: no abbreviations in identifiers. `columnName`, not `colName`; `arguments`, not `args`. Full words make grep-driven code review tractable on a single-file codebase.

### Build with sanitizers early

The `Sanitize` build type in `CMakeLists.txt` enables ASan plus a long list of UBSan checks (signed-integer-overflow, alignment, bounds, function, return, vla-bound). Run it whenever you touch the dispatch chain — `std::variant` misuse is the most common source of memory bugs in engines.

### Keep the engine to one file as long as you can

ArrowComputeEngine's entire dispatch logic fits in one ~400-line file. Splitting handlers into separate translation units adds little (every handler is short and self-contained) and costs you whole-program inlining of the dispatch chain. Only split when an operator's implementation is genuinely large (a custom hash-join, a JIT-compiled kernel, a network protocol client).

---

## 13. References

| File                                                        | What's in it                                                                  |
|-------------------------------------------------------------|-------------------------------------------------------------------------------|
| `Source/ArrowComputeEngine.cpp`                             | The whole engine — read end-to-end (~400 lines)                                |
| `Build/deps/include/BOSS.h`                                 | The C ABI types and free functions                                            |
| `Build/deps/include/BOSS.hpp`                               | The C++ delegate wrapper                                                       |
| `Build/deps/include/Expression.hpp`                         | `boss::Expression`, `ComplexExpression`, atoms, spans                          |
| `Build/deps/include/ExpressionUtilities.hpp`                | The pattern-matching DSL (`Any_`, `AnySequence_`, `Recurse`, `<`, `>=`, `>`)   |
| `Build/deps/include/Engine.hpp`                             | The minimal `Engine` interface                                                 |
| `Tests/repl-tests.scm`                                      | Operator and composition tests                                                 |
| `Tests/tpch-queries.scm`                                    | Reusable query-plan macros                                                     |
| `CMakeLists.txt`                                            | Build template — copy and rename                                               |
| `OperatorCatalog.md`                                        | Per-operator semantics and syntax                                              |

---

## Appendix: minimal viable engine

If you want to start from scratch with the smallest possible engine, this is the bones:

```cpp
#include <BOSS.hpp>
#include <Expression.hpp>
#include <ExpressionUtilities.hpp>
#include <Utilities.hpp>

using namespace boss::utilities::experimental;
using boss::ComplexExpression;
using boss::Expression;
using boss::Symbol;
using boss::utilities::operator""_;

static boss::Expression evaluate(boss::Expression&& e) {
  using boss::utilities::experimental::sentinel::AnySequence_;
  return std::move(e)
    < "Echo"_(AnySequence_) >= Recurse(evaluate) > [](auto, auto dynamics, auto) {
        return dynamics.empty() ? boss::Expression{std::string{}}
                                : std::move(dynamics.at(0));
    }
    < "GetEngineDescription"_(AnySequence_) >= Recurse(evaluate) > [](auto, auto, auto) {
        return std::string("Echo(x) — returns x unchanged.");
    };
}

extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
  return new BOSSExpression{.delegate = evaluate(std::move(e->delegate))};
}
```

This is a valid BOSS engine: it can be loaded with `SetDefaultEnginePipeline`, called via `boss-eval (Echo "hi")`, and introspected via `GetEngineDescription`. From here, add a registry (§7), then handlers (§6), then bridging (§9).
