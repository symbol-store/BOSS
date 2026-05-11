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

#ifdef _WIN32
  #define BOSS_API extern "C" __declspec(dllexport)
#else
  #define BOSS_API extern "C"
#endif

using std::string_literals::operator""s;
using boss::utilities::operator""_;
using boss::ComplexExpression;
using boss::Span;
using boss::Symbol;

using boss::Expression;

static Expression evaluate(Expression &&e) {
  return std::move(e);
};

BOSS_API BOSSExpression* evaluate(BOSSExpression* e) {
  return new BOSSExpression{.delegate = evaluate(std::move(e->delegate))};
};
]=]
)
