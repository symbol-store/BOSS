# BOSS

## Dependencies

You will need cmake and a (reasonably new) C++ compiler (the BOSS core requires C++ 17).

### MacOS

Assuming you already have a working installation of homebrew, just run

```bash
brew install cmake
```

## Creating a new Engine

Let's assume you want to create an engine called ReferenceEngine

```bash
git clone git@github.com:symbol-store/BOSS.git
cmake -P BOSS/CreateNewEngine.cmake -- ReferenceEngine
cmake -S ReferenceEngine -B build
cmake --build build
```

## Running an existing engine

```bash
git clone https://github.com/symbol-store/BOSS
cmake -S BOSS -B build -DBOSS_DEFAULT_ENGINES=ArrowComputeEngine
cmake --build build
cd build
curl -sL 'https://github.com/owid/covid-19-data/blob/master/public/data/owid-covid-data.csv?raw=true' \
  | cut -d, -f1,4,12 > owid-covid-data.csv
./boss -p '(ToStatus (Name (Materialize (OrderBy (Project (Load "owid-covid-data.csv") iso_code date (Int date) new_cases_per_million) (keys iso_code |int(date)|))) sorted))' -p '(Join (GroupBy (Name (Pairwise (Cumulate (ByName sorted) (sum new_cases_per_million)) smoothed_new_cases_per_million |sum(new_cases_per_million)| 7) smoothed) (max smoothed_new_cases_per_million) date) (keys date |max(smoothed_new_cases_per_million)|) (ByName smoothed) (keys date smoothed_new_cases_per_million))'
```

## Testing your engine

```bash
./build/deps/bin/Tests --library build/libReferenceEngine.so
```

Obviously, many tests will fail and you need to implement whatever functionality you would like your engine to support.

## Running the Scheme tests

The Scheme-level tests live in `Tests/repl-tests.scm` and use the `(chibi test)` framework. Run them from the repository root using the `boss` binary:

```bash
./build/deps/bin/boss Tests/repl-tests.scm
```

The test file calls `(test-exit)` at the end, so the process exits with a non-zero status if any tests fail.

## Using the Chibi Scheme REPL

After building, you can evaluate BOSS expressions interactively using the `boss` binary:

```bash
./build/deps/bin/boss -p '(ResetEngines)' -p '9' -p '"howdie"' -p '(Plus 9 1)'
```

- `-p` evaluates a BOSS expression and prints the result

To evaluate expressions using an engine, load it first with `SetDefaultEnginePipeline`, then call `-p` as usual:

```bash
./build/deps/bin/boss -p '(SetDefaultEnginePipeline "build/libReferenceEngine.so")' -p '(Plus 8 1 4 9)'
```

## Implementing a new engine

Here is an example of a fairly simple engine that only interprets a single expression: (Plus v1 v2 v3 ...)

```cpp
#include <BOSS.hpp>
#include <Expression.hpp>
#include <ExpressionUtilities.hpp>
#include <Algorithm.hpp>

using namespace boss::algorithm;
using namespace boss::utilities::experimental;
using namespace boss::utilities::experimental::sentinel;

namespace ReferenceEngine {
static boss::Expression evaluate(boss::Expression&& e) {
  return std::move(e) //
      < "Plus"_(AnySequence_) >= Recurse(evaluate) > [](auto, auto dynamics, auto) {
        return visitAccumulate(std::move(dynamics), std::int64_t{0}, [](auto&& state, auto&& arg) {
          if constexpr(std::is_same_v<std::decay_t<decltype(arg)>, std::int64_t> ||
                       std::is_same_v<std::decay_t<decltype(arg)>, std::int32_t>) {
            state += arg;
          }
          return state;
        });
      } //
  ;
} //
} // namespace ReferenceEngine

extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
  return new BOSSExpression {.delegate = ReferenceEngine::evaluate(::std::move(e->delegate))};
};
```
