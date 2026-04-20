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
git clone git@github.com:Bossdb/Core.git
cmake -P Core/CreateNewEngine.cmake -- ReferenceEngine
cmake -S ReferenceEngine -B build
cmake --build build
```

## Testing your engine

```bash
./build/deps/bin/Tests --library build/libReferenceEngine.so
```

Obviously, many tests will fail and you need to implement whatever functionality you would like your engine to support.

## Using the Chibi Scheme REPL

After building, you can evaluate BOSS expressions interactively using the bundled Chibi Scheme REPL. Run it from the build directory:

```bash
./deps/bin/chibi-scheme -I. -mBOSS -p'(begin (boss-eval (ResetEngines)) (boss-eval 9) (boss-eval "howdie") (boss-eval (Plus 9 1)))'
```

- `-I.` adds the build directory to the module search path (where `BOSS.sld` was copied)
- `-mBOSS` loads the BOSS Scheme module
- `boss-eval` converts a Scheme expression to a BOSS expression, evaluates it, and converts the result back

To evaluate expressions using an engine, load it first with `SetDefaultEnginePipeline`, then call `boss-eval` as usual:

```bash
./deps/bin/chibi-scheme -I. -mBOSS -p'(begin (boss-eval (SetDefaultEnginePipeline "build/libReferenceEngine.so")) (boss-eval (Plus 8 1 4 9)))'
```

## Implementing a new engine

Here is an example of a fairly simple engine that only interprets a single expression: (Plus v1 v2)

```cpp
#include <Algorithm.hpp>
#include <BOSS.hpp>

using namespace std;
using namespace boss::algorithm;

namespace boss::storage::git {
boss::Expression evaluate(boss::Expression&& e) {
  return visit(
      [](auto&& e) -> boss::Expression {
        if constexpr(isComplexExpression<decltype(e)>) {
          boss::ExpressionArguments args = e.getArguments();
          visitTransform(args, [](auto&& arg) -> boss::Expression {
            if constexpr(isComplexExpression<decltype(arg)>) {
              return evaluate(std::move(arg));
            } else {
              return std::forward<decltype(arg)>(arg);
            }
          });
          if(e.getHead() == Symbol("Plus")) {
            return visitAccumulate(move(args), 0L, [](auto&& state, auto&& arg) {
              if constexpr(is_same_v<decay_t<decltype(arg)>, long long>) {
                state += arg;
              }
              return state;
            });
          } else {
            return boss::ComplexExpression(e.getHead(), {}, std::move(args), {});
          }
        } else {
          return forward<decltype(e)>(e);
        }
      },
      move(e));
}
} // namespace boss::storage::git

extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
  return new BOSSExpression{.delegate = boss::storage::git::evaluate(::std::move(e->delegate))};
};
```
