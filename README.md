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

## Testing your engine

```bash
./build/deps/bin/Tests --library build/libReferenceEngine.so
```

Obviously, many tests will fail and you need to implement whatever functionality you would like your engine to support.

## Running the Scheme tests

The Scheme-level tests live in `Tests/repl-tests.scm` and use the `(chibi test)` framework. Run them from the repository root using the `boss` binary:

```bash
./build/boss Tests/repl-tests.scm
```

The test file calls `(test-exit)` at the end, so the process exits with a non-zero status if any tests fail.

## Using the Chibi Scheme REPL

After building, you can evaluate BOSS expressions interactively using the `boss` binary:

```bash
./build/boss -p'(begin (boss-eval (ResetEngines)) (boss-eval 9) (boss-eval "howdie") (boss-eval (Plus 9 1)))'
```

- `boss-eval` converts a Scheme expression to a BOSS expression, evaluates it, and converts the result back

To evaluate expressions using an engine, load it first with `SetDefaultEnginePipeline`, then call `boss-eval` as usual:

```bash
./build/boss -p'(begin (boss-eval (SetDefaultEnginePipeline "build/libReferenceEngine.so")) (boss-eval (Plus 8 1 4 9)))'
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

namespace ReferenceEngine {
static boss::Expression evaluate(boss::Expression&& e) {
  return std::move(e) //
      < "Plus"_(AnySequence_) >= Recurse(evaluate) > [](auto, auto dynamics, auto) {
        return visitAccumulate(std::move(dynamics), 0L, [](auto&& state, auto&& arg) {
          if constexpr(std::is_same_v<std::decay_t<decltype(arg)>, std::int32_t> ||
                       std::is_same_v<std::decay_t<decltype(arg)>, std::int64_t>) {
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
