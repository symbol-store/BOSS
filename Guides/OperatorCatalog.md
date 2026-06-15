# BOSS Operator Catalog (as implemented by ArrowComputeEngine)

This catalog describes the operators that ArrowComputeEngine implements. It is **descriptive**, not normative: it documents the syntax, semantics, and edge cases of one engine's vocabulary, so that new engines targeting similar relational workloads can reuse the conventions where they fit.

There is no central BOSS specification of "the" operator set. Each engine is free to define its own. Interoperability is much easier when engines reuse the same operator names and shapes for the same semantics — reuse when it fits, deviate explicitly when it does not, and document any deviation in your engine's `GetEngineDescription` output.

For implementation guidance, see [`EngineImplementationGuide.md`](EngineImplementationGuide.md).

---

## Conventions

- **Operator heads** are written in PascalCase (`Filter`, `OrderBy`, `GroupBy`, `LeftJoin`). Internally, ArrowComputeEngine lowercases them and applies aliases (see `toArrowName` in the source).
- **Symbols inside operator arguments** are column references if they match a column name in the input schema, and symbol-valued literals otherwise. This is what lets `(Equal sport Running)` parse as "the `sport` column equals the symbol `Running`" without needing an explicit quote.
- **String literals** are always values: `(Filter t (Equal name "Alice"))`.
- **Auto-cast for date literals**: bare `YYYY-MM-DD` string literals appearing as the right-hand side of a comparison are auto-cast to Arrow `date32`.
- **Output column names** for aggregates follow the form `fn(col)`, e.g. `sum(quantity)`. They become BOSS symbols that, because they contain parentheses, must be quoted with `|…|` syntax in Scheme: `|sum(quantity)|`.
- **Pipeline composition**: every operator that consumes a "table" accepts any other operator that produces a table. There is no separate plan-builder mode.
- **Lazy execution**: most operators build a plan fragment and defer execution. `Materialize`, `ToStatus`, and the final `convertResult` at the C ABI boundary are the points at which a plan is actually run.

---

## Ingestion

### `Load(path [col …])`

Reads a CSV file from `path` into an in-memory table.

- `path` is a string.
- Files ending in `.tbl` are parsed with `|` delimiters (TPC-H convention), with an extra trailing-empty-column workaround.
- An optional list of column-name symbols restricts the load to those columns: `(Load "file.csv" id name email)`.
- Returns a handle into the engine's registry.

**Directory mode.** If `path` is a directory, every `.csv` and `.tbl` file at the top level is loaded and unioned:

- The result schema is the union of all per-file schemas; columns absent in a given file are filled with nulls (which surface as `NULL` symbols when materialized).
- A `"file name"` column of `boss_type=symbol` is appended to every row with the basename of the file it came from (extension included).
- Optional column-name arguments are applied to every file (same `include_columns` list per CSV).
- A directory with no `.csv`/`.tbl` files produces an empty table.

**Example:**

```scheme
(Load "owid-covid-data.csv")
(Load "lineitem.tbl" l_orderkey l_partkey l_quantity)
(Load "TPCHData/")  ; loads every .csv/.tbl in the directory, tagged by file name
```

### `Table((col v1 v2 …) …)`

Constructs a table from inline literal data. Each child is a column expression whose head is the column name and whose values are the column data.

- Column type is inferred from the first non-symbol value (int64, float64, string, bool).
- A `Symbol` value inside a typed column is treated as `NULL` (the "named-null" idiom: `(A 1 NULL 3)` parses with `NULL` as a Symbol since it is unquoted).
- A column whose values are *all* symbols becomes a string-typed column with `boss_type=symbol` Arrow field metadata, so it round-trips as symbols rather than strings.
- Span arguments (typed columnar slices) are accepted in addition to dynamic arguments; the Arrow engine's `Table` handler walks both.

**Example:**

```scheme
(Table (A 1 2 3) (B 4.0 5.0 6.0))
(Table (sport Running Swimming) (distance 5 10))
(Table (flags #t NULL #f))
```

---

## Reshape

### `Project(table col …)`

Selects and renames columns. Each subsequent argument is either:

- A bare column symbol — keep the column as-is.
- `(As expr name)` — compute `expr` and bind the result to `name`.
- A compute expression — keep the result, naming it after the expression's text form (often unwieldy; prefer `As`).

Supports type casts via `(Int col)`, `(Bool col)`, `(Date col)`, `(Timestamp col)`, and any Arrow compute function as a head (`Add`, `Subtract`, `Multiply`, `IfElse`, `Year`, `Month`, …).

**Example:**

```scheme
(Project orders custkey (As (Multiply price (Subtract 1.0 discount)) revenue))
```

### `Filter(table predicate)`

Keeps rows where `predicate` evaluates true. `predicate` is built from:

- Comparison heads: `Equal`, `NotEqual`, `Less`, `LessEqual`, `Greater`, `GreaterEqual`.
- Combinators: `And`, `Or`, `Not`.
- `(Between val lo hi)` — desugared to `(And (GreaterEqual val lo) (LessEqual val hi))`.
- `(Like col pattern)` — SQL `%`/`_` wildcard match via Arrow's `match_like`.
- Arbitrary Arrow compute functions used as the head.

**Example:**

```scheme
(Filter lineitem (And (Greater discount 0.05) (Less discount 0.07)))
(Filter customers (Like name "Al%"))
(Filter shipments (Between ship_date "1996-01-01" "1996-12-31"))
```

### `Slice(table offset count)`

Returns rows `[offset, offset+count)`. Both arguments must be integer literals.

**Example:**

```scheme
(Slice (OrderBy parts (keys price)) 0 10)   ; cheapest 10
```

---

## Ordering

### `OrderBy(table (keys col …))`

Sorts by the listed keys in order of precedence. A bare symbol is ascending; `(Desc col)` is descending.

**Example:**

```scheme
(OrderBy sales (keys region (Desc revenue)))
```

---

## Aggregation

### `GroupBy(table (agg col) … [key …])`

Aggregates one or more columns. Each aggregator is a unary expression naming a column to reduce (`(Sum x)`, `(Mean x)`, `(Max x)`, `(Min x)`, `(Count x)`) or the nullary `(CountAll)`.

- Aggregator heads are mapped through `toArrowName`: `Avg` → `mean`, `CountAll` → `count_all`.
- Group keys, if any, follow all aggregators as bare symbols.
- Without keys, it is a global aggregate (single-row output).
- Output columns are named `fn(col)` (e.g. `sum(quantity)`); rename them in a subsequent `Project (As …)` for ergonomic results.

**Example:**

```scheme
(GroupBy lineitem (Sum quantity) (CountAll) returnflag linestatus)
```

**HAVING** is expressed as `Filter` after `GroupBy`:

```scheme
(Filter (GroupBy orders (Sum quantity) orderkey) (Greater |sum(quantity)| 300))
```

---

## Sliding and cumulative

These operators are order-sensitive — the input table must already be in the order you want the window to walk. They are also ArrowComputeEngine's example of operators that bypass the underlying planner (Acero) because the planner cannot express them, and use scalar compute kernels directly. See §12 "escape hatch" in the implementation guide.

### `Cumulate(table (agg col))`

Appends a column containing the running prefix aggregate (e.g. cumulative sum) of the named column. Output column is named `fn(col)`.

**Example:**

```scheme
(Cumulate sales (Sum revenue))    ; appends |sum(revenue)| as a running total
```

### `Pairwise(table out-col in-col lag)`

Appends a column `out-col` whose value at row `i` is `in-col[i] − in-col[i − lag]`. The first `lag` rows are `NULL`.

**Example:**

```scheme
(Pairwise (Cumulate sales (Sum revenue)) smoothed_revenue |sum(revenue)| 7)
```

---

## Joins

All join variants use the same predicate syntax: one or more `(Equal lCol rCol)` predicates define equi-join keys, and any additional predicates are ANDed together as a residual filter applied after the hash lookup. Colliding output column names get `_l` / `_r` suffixes.

### `Join(left right pred …)`

Inner hash join.

**Warning:** omitting all `Equal` predicates degrades to an O(n²) cross-join. The engine inserts a constant dummy key column on both sides and applies the predicate as a post-join filter. Useful for `(Between val lo hi)`-style joins, but expensive on large inputs.

**Example:**

```scheme
(Join orders lineitems (Equal orderkey orderkey))
(Join lineitems brackets (Equal product product) (Between price low high))
```

### `LeftJoin(left right pred …)`

Left outer hash join. Left rows with no match get `NULL` for right columns.

**Example:**

```scheme
(LeftJoin customers orders (Equal custkey custkey))
```

### `AntiJoin(left right pred …)`

Left anti join. Returns left rows with *no* match in right.

**Example:**

```scheme
(AntiJoin parts disqualified_suppliers (Equal suppkey suppkey))
```

---

## Set operations

### `Union(table table …)`

Concatenates two or more tables (bag union — duplicates are preserved, matching SQL `UNION ALL`). Schemas are unified across inputs: columns absent from a given input are filled with nulls (which surface as `NULL` symbols when materialized). Rows appear in input order: `(Union a b c)` yields a's rows, then b's, then c's. For set-style union without duplicates, follow with `GroupBy` on all columns.

**Example:**

```scheme
(Union (Filter orders (Equal region "US"))
       (Filter orders (Equal region "EU")))
```

---

## Named bindings (DAG-shaped plans)

### `Name(table sym)`

Stores `table` under the symbol `sym` in the engine's name registry and marks it as live for the engine's garbage collector. Returns the table (so it can be threaded through a larger expression).

### `ByName(sym)`

Retrieves the table previously stored under `sym`. This lets the user express shared subplans as named bindings, turning a DAG edge into a tree reference.

**Example — shared subplan:**

```scheme
(boss-eval (Name (GroupBy lineitem (Min supplycost)) min_cost))
(boss-eval (Filter partsupp (Equal supplycost (ByName min_cost))))
(boss-eval (Filter parts    (Greater retail   (ByName min_cost))))
```

**Example — multi-statement pipeline (the Covid hotspot query):**

```scheme
(Name (Materialize (OrderBy (Project (Load "data.csv") iso_code date) (keys iso_code))) sorted)
(Name (Pairwise (Cumulate (ByName sorted) (Sum cases)) smoothed sum_cases 7) smoothed)
(Join (GroupBy (ByName smoothed) (Max smoothed) date) (ByName smoothed)
      (Equal date date) (Equal max_smoothed smoothed))
```

---

## Materialization

### `Materialize(table)`

Forces evaluation of any deferred plan into a single contiguous Arrow table. Useful when you intend to repeatedly query a result, so the plan is computed once.

### `ToStatus(table)`

Evaluates a pipeline and returns the literal string `"OK"` instead of the materialized result. Useful during profiling: you measure the plan execution time without paying the result-conversion cost (which can dominate for large outputs).

**Example:**

```scheme
(ToStatus (Filter (Load "big.csv") (Greater age 18)))   ; runs the plan, prints "OK"
```

---

## Introspection

### `Schema(table)`

Returns a one-column table whose column is named `Columns` and whose values are the schema-column names of `table` as symbols.

**Example:**

```scheme
(Schema (Table (A 1) (B 2) (C 3)))
;; => (Table (Columns A B C))
```

### `GetEngineDescription()`

Returns a multi-line string listing every supported operator and its signature. The text in `ArrowComputeEngine.cpp` is the source of truth for what this engine reports; keep this catalog and the in-source string in sync.

---

## Reserved scalar / compute heads

These are not standalone operators but appear inside `Filter` predicates and `Project` expressions. They map to Arrow compute kernels and are listed here for reference; an engine wrapping a different library can substitute equivalents.

| BOSS head                | Maps to (Arrow)         | Notes                                      |
|--------------------------|-------------------------|--------------------------------------------|
| `Add`, `Subtract`, `Multiply`, `Divide` | same                    | arithmetic                                 |
| `Equal`, `NotEqual`      | `equal`, `not_equal`    |                                            |
| `Less`, `LessEqual`      | `less`, `less_equal`    |                                            |
| `Greater`, `GreaterEqual`| `greater`, `greater_equal` |                                         |
| `And`, `Or`              | same                    |                                            |
| `Not`                    | `invert`                |                                            |
| `IfElse`                 | `if_else`               | `(IfElse cond then else)`                  |
| `IsValid`                | `is_valid`              | null check                                 |
| `Int`, `Bool`, `Date`, `Timestamp` | `cast`        | type casts                                 |
| `Like`                   | `match_like`            | `(Like col "pat%")`                        |
| `Between`                | (desugared)             | `(Between v lo hi)` → `(And ≥ ≤)`          |
| `Year`, `Month`, `Day`   | same                    | temporal extractors on `timestamp[s, UTC]` |

Aggregator heads inside `GroupBy` / `Cumulate`:

| BOSS head     | Maps to (Arrow)              |
|---------------|------------------------------|
| `Sum`         | `sum` (or `hash_sum`)        |
| `Mean`, `Avg` | `mean` / `hash_mean`         |
| `Min`         | `min` / `hash_min`           |
| `Max`         | `max` / `hash_max`           |
| `Count`       | `count` / `hash_count`       |
| `CountAll`    | `count_all` / `hash_count_all` |

(The `hash_` prefix is applied automatically by ArrowComputeEngine when group keys are present.)

---

## Notes on extensibility

A new engine MAY:

- Add operators not in this catalog (`LoadParquet`, `KMeans`, `BFS`, `EmbedText`, …). Use a head name that does not collide with any documented operator.
- Implement only a subset of these operators if its target workload does not need the rest.
- Re-interpret an operator's semantics so long as the deviation is documented in the engine's own `GetEngineDescription` output.

A new engine SHOULD, even if its vocabulary is domain-specific:

- Implement `GetEngineDescription` — it is the BOSS REPL's introspection contract.
- Implement `Schema` and `Table` — these let the user round-trip data through the engine without external files, which is the basis of the test harness pattern in §11 of the implementation guide.
- Implement `ToStatus` — useful for benchmarking and for compositional pipelines where the engine's output feeds into another engine that just needs an OK signal.

An engine MUST avoid:

- Throwing exceptions across the `extern "C"` boundary. Return error strings instead (see implementation guide §8).
- Reusing an operator head with different semantics than this catalog without also overriding `GetEngineDescription` to make the divergence visible to the user.
