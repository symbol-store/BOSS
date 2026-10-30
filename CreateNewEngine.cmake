math(EXPR LAST_ARGC "${CMAKE_ARGC} - 1") 

set(PROJECT_NAME "${CMAKE_ARGV${LAST_ARGC}}")
message("generating new project: ${PROJECT_NAME}")

execute_process(COMMAND git branch --show-current WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} OUTPUT_VARIABLE BOSS_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git config --get branch.${BOSS_BRANCH}.remote WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} OUTPUT_VARIABLE BOSS_REMOTE_NAME OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git config --get remote.${BOSS_REMOTE_NAME}.url WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} OUTPUT_VARIABLE BOSS_REMOTE_URL OUTPUT_STRIP_TRAILING_WHITESPACE)

file(WRITE ${PROJECT_NAME}/CMakeLists.txt
"cmake_minimum_required(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})\n"
"project(${PROJECT_NAME})\n"
[=[

############################## Custom build modes ###############################

set(CMAKE_CXX_FLAGS_SANITIZE "-fno-omit-frame-pointer -fsanitize=address,signed-integer-overflow,null,alignment,bounds,function,return,vla-bound -O0 -g"
  CACHE STRING
  "Flags used by the C++ compiler during Sanitize builds."
  FORCE)
set(CMAKE_C_FLAGS_SANITIZE "-fno-omit-frame-pointer -fsanitize=address,signed-integer-overflow,null,alignment,bounds,function,return,vla-bound -O0 -g"
  CACHE STRING
  "Flags used by the C compiler during Sanitize builds."
  FORCE)
set(CMAKE_EXE_LINKER_FLAGS_SANITIZE
  ${CMAKE_EXE_LINKER_FLAGS_DEBUG} CACHE STRING
  "Flags used for linking binaries during Sanitize builds."
  FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_SANITIZE
  ${CMAKE_SHARED_LINKER_FLAGS_DEBUG} CACHE STRING
  "Flags used by the shared libraries linker during Sanitize builds."
  FORCE)
mark_as_advanced(
  CMAKE_CXX_FLAGS_SANITIZE		  CMAKE_EXE_LINKER_FLAGS_SANITIZE
  CMAKE_C_FLAGS_SANITIZE		  CMAKE_SHARED_LINKER_FLAGS_SANITIZE
  )

set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
  "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel Sanitize."
  FORCE)

############################### External Projects ###############################

include(ExternalProject)
]=]

"set(BOSS_SOURCE_BRANCH ${BOSS_BRANCH} CACHE STRING \"if boss core is built from source, which branch should be fetched\")
set(BOSS_SOURCE_REPOSITORY ${BOSS_REMOTE_URL} CACHE STRING \"url for the boss repository\")
ExternalProject_Add(BOSS
  GIT_REPOSITORY \${BOSS_SOURCE_REPOSITORY}
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads/\${CMAKE_PROJECT_NAME}
  GIT_TAG \${BOSS_SOURCE_BRANCH}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=\${${PROJECT_NAME}_BINARY_DIR}/deps $<IF:$<CONFIG:>,,-DCMAKE_BUILD_TYPE=\${CMAKE_BUILD_TYPE}> -DCMAKE_CXX_COMPILER=\${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=\${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=\${CMAKE_CXX_FLAGS}
  )


#################################### Targets ####################################
if(MSVC)
  # not making any difference on Windows
  # and helps Visual Studio to parse it correctly as a target
  set(LibraryType SHARED)
else()
  set(LibraryType MODULE)
endif(MSVC)
add_library(${PROJECT_NAME} \${LibraryType} Source/${PROJECT_NAME}.cpp)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)
if(MSVC)
  target_compile_options(${PROJECT_NAME} PUBLIC \"/Zc:__cplusplus\")
  target_compile_options(${PROJECT_NAME} PUBLIC \"/EHsc\")
endif(MSVC)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC \${${PROJECT_NAME}_BINARY_DIR}/deps/include)
add_dependencies(${PROJECT_NAME} BOSS)

set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
"
)

file(WRITE ${PROJECT_NAME}/Source/${PROJECT_NAME}.cpp
[=[
#include <BOSS.hpp>
#include <Expression.hpp>
#include <ExpressionUtilities.hpp>
#include <Utilities.hpp>

using std::string_literals::operator""s;
using boss::utilities::operator""_;
using boss::ComplexExpression;
using boss::Span;
using boss::Symbol;

using boss::Expression;

static Expression evaluate(Expression &&e) {
  return std::move(e);
};

extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
  if(e == nullptr) {
    return nullptr;
  }
  return new BOSSExpression{.delegate = evaluate(std::move(e->delegate))};
};
]=]
)

file(WRITE ${PROJECT_NAME}/README.md
"# ${PROJECT_NAME}\n"
[=[

A BOSS engine. BootstrapEngine loads this library and calls its `evaluate` function.

## Contract

- BootstrapEngine frees the answer of `evaluate` with `freeBOSSExpression`, so `evaluate` must
  allocate the answer with `new BOSSExpression`.
- `evaluate` may answer `nullptr`. BootstrapEngine passes that `nullptr` on to the next engine in
  the pipeline, so `evaluate` must also accept `nullptr` as its input.
- BootstrapEngine reports a `std::exception` that `evaluate` throws as an
  `ErrorWhenEvaluatingExpression`.
- If the library exports `extern "C" void reset()`, BootstrapEngine calls `reset` before it
  unloads the library. On Linux and macOS, BootstrapEngine opens the library with
  `RTLD_NODELETE`, so a later load does not run the static initializers again, and `reset` must
  clear the engine's state.

## Threading

BootstrapEngine may call `evaluate` from several threads at the same time and holds no lock
during the call, so `evaluate` must be reentrant.

## Custom entry points

A pipeline entry can name this engine by a complex expression, such as `"MyEngine"_("Fast"_)`.
Before each dispatch through that entry, BootstrapEngine calls `evaluate` with
`GetEntryPoint("Fast"_)`. If `evaluate` answers with a non-zero `int64` that holds the address of
a function with the signature of `evaluate`, BootstrapEngine calls that function instead of
`evaluate`. Any other answer keeps `evaluate`.

## Pattern matching

`ExpressionUtilities.hpp` provides a dispatch chain:

```cpp
using namespace boss::utilities::experimental;
using namespace boss::utilities::experimental::sentinel;

return std::move(e)
  < "Plus"_(AnySequence_) >= Recurse(evaluate) > [](auto head, auto dynamics, auto spans) { ... }
  < "Load"_(String_) > [](auto head, auto dynamics, auto spans) { ... };
```

| Pattern               | Matches                                   |
|-----------------------|-------------------------------------------|
| `"Op"_(AnySequence_)` | `Op` with any number of arguments         |
| `"Op"_(Any_, Any_)`   | `Op` with exactly two arguments           |
| `"Op"_()`             | `Op` with no arguments                    |
| `"Op"_(Symbol_)`      | `Op` with one symbol argument             |
| `"Op"_(String_)`      | `Op` with one string argument             |
| `"Op"_(Integer_)`     | `Op` with one integer argument            |

The chain calls the handler of the first pattern that matches and returns an expression that
matches no pattern unchanged. `>= Recurse(evaluate)` evaluates the dynamic arguments before the
handler runs.

## Loading engines

Several loading engines in one pipeline can all match `Load`. A loading engine checks the path
before it consumes any argument, and answers the unchanged `Load` expression when the path holds
no data in its format. When a directory holds data in several formats, the engine answers
`Union(Load(path), Table(...))` with the data that the engine read, so that the next engine
reads the rest. `Symbol("NULL")` marks a missing value.
]=]
)
