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

<details open>
<summary><b>Linux/MacOS</b></summary>

```bash
git clone git@github.com:symbol-store/BOSS.git
cmake -P BOSS/CreateNewEngine.cmake -- ReferenceEngine
cmake -S ReferenceEngine -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

<details>
<summary><b>Windows (MSVC)</b></summary>

```bash
(in the Developer Command Prompt)
git clone git@github.com:symbol-store/BOSS.git
cmake -P BOSS/CreateNewEngine.cmake -- ReferenceEngine
cmake -S ReferenceEngine -B build -DDISABLE_REPL=ON
cmake --build build
```

<details>
<summary><b>Windows (Clang-Cl)</b></summary>

```bash
(in the x64 Native Tools Command Prompt)
git clone git@github.com:symbol-store/BOSS.git
cmake -P BOSS/CreateNewEngine.cmake -- ReferenceEngine
cmake -S ReferenceEngine -B build -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DDISABLE_REPL=ON
cmake --build build
```

## Testing your engine

<details open>
<summary><b>Linux/MacOS</b></summary>

```bash
./build/deps/bin/Tests --library build/libReferenceEngine.so
```

<details>
<summary><b>Window</b></summary>

```bash
.\build\deps\bin\Tests.exe --library .\build\ReferenceEngine-win.dll
```

Obviously, many tests will fail and you need to implement whatever functionality you would like your engine to support.

## Running the Scheme tests

The Scheme-level tests live in `Tests/repl-tests.scm` and use the `(chibi test)` framework. Run them from the repository root using the bundled `chibi-scheme` binary:

<details open>
<summary><b>Linux/MacOS</b></summary>

```bash
./build/deps/bin/chibi-scheme Tests/repl-tests.scm
```

The test file calls `(test-exit)` at the end, so the process exits with a non-zero status if any tests fail.

## Using the Chibi Scheme REPL

After building, you can evaluate BOSS expressions interactively using the bundled Chibi Scheme REPL:

<details open>
<summary><b>Linux/MacOS</b></summary>

```bash
./build/deps/bin/chibi-scheme -mBOSS -p'(begin (boss-eval (ResetEngines)) (boss-eval 9) (boss-eval "howdie") (boss-eval (Plus 9 1)))'
```

- `-mBOSS` loads the BOSS Scheme module
- `boss-eval` converts a Scheme expression to a BOSS expression, evaluates it, and converts the result back

To evaluate expressions using an engine, load it first with `SetDefaultEnginePipeline`, then call `boss-eval` as usual:

<details open>
<summary><b>Linux/MacOS</b></summary>

```bash
./build/deps/bin/chibi-scheme -mBOSS -p'(begin (boss-eval (SetDefaultEnginePipeline "build/libReferenceEngine.so")) (boss-eval (Plus 8 1 4 9)))'
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
      <"Plus" >= Recurse(evaluate)>[](auto, auto dynamics, auto) {
        return visitAccumulate(std::move(dynamics), int64_t{0}, [](auto&& state, auto&& arg) {
          if constexpr(std::is_same_v<std::decay_t<decltype(arg)>, int>) {
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
