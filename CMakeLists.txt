cmake_minimum_required(VERSION 3.24)
project(BOSS)

########################### C++ Compiler Requirements ###########################

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "8.0")
    message(FATAL_ERROR "Your GCC version has this bug which affects this project: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68177")
  endif()
endif()


############################## Custom build modes ###############################

set(SANITIZATION_FLAGS "-fsanitize=address,signed-integer-overflow,null,alignment,bounds,function,return,vla-bound")

set(CMAKE_CXX_FLAGS_SANITIZE "-fno-omit-frame-pointer ${SANITIZATION_FLAGS} -O0 -g"
  CACHE STRING
  "Flags used by the C++ compiler during Sanitize builds."
  FORCE)
set(CMAKE_C_FLAGS_SANITIZE "-fno-omit-frame-pointer ${SANITIZATION_FLAGS} -O0 -g"
  CACHE STRING
  "Flags used by the C compiler during Sanitize builds."
  FORCE)
set(CMAKE_EXE_LINKER_FLAGS_SANITIZE
  "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${SANITIZATION_FLAGS}" CACHE STRING
  "Flags used for linking binaries during Sanitize builds."
  FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_SANITIZE
  "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} ${SANITIZATION_FLAGS}" CACHE STRING
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

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR
   CMAKE_INSTALL_PREFIX STREQUAL "/usr/local" OR
   CMAKE_INSTALL_PREFIX STREQUAL "C:/Program Files/${CMAKE_PROJECT_NAME}")
  set(_chibi_prefix "${CMAKE_BINARY_DIR}/deps")
else()
  set(_chibi_prefix "${CMAKE_INSTALL_PREFIX}")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(GOOGLEBENCHMARK_USE_LIBCXX YES CACHE BOOL "build google benchmark with libc++")
else()
  set(GOOGLEBENCHMARK_USE_LIBCXX NO CACHE BOOL "build google benchmark with libc++")
endif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")


ExternalProject_Add(googlebenchmark
	URL "https://github.com/google/benchmark/archive/refs/tags/v1.7.1.tar.gz"
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads/${CMAKE_PROJECT_NAME}
  DOWNLOAD_EXTRACT_TIMESTAMP NEW
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON -DBENCHMARK_ENABLE_GTEST_TESTS=OFF $<IF:$<CONFIG:>,,-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}> -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark${CMAKE_STATIC_LIBRARY_SUFFIX} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main${CMAKE_STATIC_LIBRARY_SUFFIX}
)
set_target_properties(googlebenchmark PROPERTIES EXCLUDE_FROM_ALL TRUE)


ExternalProject_Add(catch2
	URL https://github.com/catchorg/Catch2/archive/v3.12.0.tar.gz
  URL_HASH SHA256=e077079f214afc99fee940d91c14cf1a8c1d378212226bb9f50efff75fe07b23
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads/${CMAKE_PROJECT_NAME}
  DOWNLOAD_EXTRACT_TIMESTAMP NEW
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCATCH_BUILD_TESTING=NO -DBUILD_TESTING=NO -DCATCH_ENABLE_WERROR=NO -DCATCH_INSTALL_DOCS=NO -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}Catch2$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX}
  )


ExternalProject_Add(spdlog
	URL https://github.com/gabime/spdlog/archive/refs/tags/v1.8.5.tar.gz
  URL_HASH SHA256=944d0bd7c763ac721398dca2bb0f3b5ed16f67cef36810ede5061f35a543b4b8
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads/${CMAKE_PROJECT_NAME}
  DOWNLOAD_EXTRACT_TIMESTAMP NEW
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -DSPDLOG_BUILD_EXAMPLE=OFF
)


# REPL support via Chibi Scheme.
# DISABLE_REPL (user-facing): OFF by default (REPL enabled).
#   If not explicitly set and make is not found: warn and skip.
#   If explicitly set to OFF and make is not found: fatal error.
#   If explicitly set to ON: REPL skipped unconditionally.
set(_DISABLE_REPL_EXPLICITLY_SET OFF)
foreach(_cmake_argv IN LISTS CMAKE_ARGV)
  if(_cmake_argv MATCHES "^-DDISABLE_REPL(:[^=]+)?=")
    set(_DISABLE_REPL_EXPLICITLY_SET ON)
    break()
  endif()
endforeach()
option(DISABLE_REPL "Disable the Chibi Scheme REPL" OFF)

find_program(MAKE_EXECUTABLE NAMES gmake mingw32-make make NAMES_PER_DIR DOC "GNU Make")
if(MAKE_EXECUTABLE AND NOT MAKE_EXECUTABLE MATCHES "-NOTFOUND$")
  set(MAKE_FOUND ON)
else()
  set(MAKE_FOUND OFF)
endif()

if(NOT DISABLE_REPL AND NOT MAKE_FOUND)
  if(_DISABLE_REPL_EXPLICITLY_SET)
    message(FATAL_ERROR
      "DISABLE_REPL=OFF requires GNU Make to build Chibi Scheme, but none was found. "
      "Please install GNU Make (or a compatible make tool).")
  else()
    message(WARNING
      "==================================================================\n"
      "  GNU Make not found -- Chibi Scheme REPL will NOT be built.\n"
      "  Install GNU Make or set -DDISABLE_REPL=ON to silence this.\n"
      "==================================================================")
  endif()
endif()

if(CMAKE_BUILD_TYPE)
  string(TOUPPER "_${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_TOUPPER)
else()
  set(CMAKE_BUILD_TYPE_TOUPPER "")
endif()

if(NOT DISABLE_REPL AND MAKE_FOUND)
  ExternalProject_Add(chibi-scheme
    URL https://github.com/ashinn/chibi-scheme/archive/refs/tags/0.12.tar.gz
    URL_HASH SHA256=b70a1147bc70a0f90df3fb6081bc99808237fd17a9accf9ee7a2cc20d95a4df0
    DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads/${CMAKE_PROJECT_NAME}
    DOWNLOAD_EXTRACT_TIMESTAMP NEW
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${MAKE_EXECUTABLE} -j 1 -C <SOURCE_DIR> libchibi-scheme.a prefix=${_chibi_prefix} "CFLAGS=${CMAKE_C_FLAGS${CMAKE_BUILD_TYPE_TOUPPER}}" "LDFLAGS=${CMAKE_EXE_LINKER_FLAGS${CMAKE_BUILD_TYPE_TOUPPER}}" CC=${CMAKE_C_COMPILER} LD=${CMAKE_LINKER}
    INSTALL_COMMAND ${MAKE_EXECUTABLE} -C <SOURCE_DIR> install prefix=${_chibi_prefix} "LDFLAGS=${CMAKE_EXE_LINKER_FLAGS${CMAKE_BUILD_TYPE_TOUPPER}}" CC=${CMAKE_C_COMPILER}
    BUILD_BYPRODUCTS ${_chibi_prefix}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}chibi-scheme${CMAKE_SHARED_LIBRARY_SUFFIX} ${_chibi_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}chibi-scheme${CMAKE_STATIC_LIBRARY_SUFFIX} ${_chibi_prefix}/bin/chibi-ffi ${_chibi_prefix}/include/chibi/eval.h
  )
endif()

########################## Other Library Dependencies ##########################

set(VTune_DIR "" CACHE PATH "Where to look for VTune installation")
find_path(ITT_NOTIFY_INCLUDE_DIR "ittnotify.h" HINTS ${VTune_DIR}/include)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

############################### Executable Targets ###############################

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(ImplementationFiles Source/BOSS.cpp Source/Serialization.cpp)
set(TestFiles Tests/BOSSTests.cpp Tests/CAPITests.cpp)
set(BenchmarkFiles Benchmarks/BOSSBenchmarks.cpp)

if(NOT DISABLE_REPL AND MAKE_FOUND)
  set(REPLStubFiles chibi-bindings.stub)
  set(REPLFiles ${CMAKE_CURRENT_BINARY_DIR}/Source/Shims/chibi-bindings.c)
endif()

# BOSS

add_library(BOSS SHARED ${ImplementationFiles} ${REPLFiles})

if(NOT DISABLE_REPL AND MAKE_FOUND)
  add_dependencies(BOSS chibi-scheme)
  set_source_files_properties(${REPLFiles} PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/Source)

  if(WIN32)
    string(REPLACE ";" "\\;" _chibi_ffi_windows_path "$ENV{PATH}")
    set(_chibi_ffi_env
      PATH=${_chibi_prefix}/bin\;${_chibi_prefix}/lib\;${_chibi_ffi_windows_path})
  else()
    set(_chibi_ffi_env
      LD_LIBRARY_PATH=${_chibi_prefix}/lib:$ENV{LD_LIBRARY_PATH}
      DYLD_LIBRARY_PATH=${_chibi_prefix}/lib:$ENV{DYLD_LIBRARY_PATH}
      PATH=${_chibi_prefix}/bin/:$ENV{PATH})
  endif()

  add_custom_command(
    OUTPUT ${REPLFiles}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/Source/Shims
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/Source/Shims/${REPLStubFiles}
            ${CMAKE_CURRENT_BINARY_DIR}/Source/Shims/${REPLStubFiles}
    COMMAND ${CMAKE_COMMAND} -E env ${_chibi_ffi_env}
            ${_chibi_prefix}/bin/chibi-ffi ${CMAKE_CURRENT_BINARY_DIR}/Source/Shims/${REPLStubFiles}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/Shims/${REPLStubFiles} ${_chibi_prefix}/bin/chibi-ffi ${_chibi_prefix}/include/chibi/eval.h
    VERBATIM)

  target_include_directories(BOSS SYSTEM PRIVATE ${_chibi_prefix}/include)
  target_link_libraries(BOSS
    ${_chibi_prefix}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}chibi-scheme${CMAKE_SHARED_LIBRARY_SUFFIX})

  # Standalone BOSS REPL binary (statically linked)
  add_executable(boss-repl Source/boss-repl.cpp ${ImplementationFiles} ${REPLFiles})
  add_dependencies(boss-repl chibi-scheme)
  target_include_directories(boss-repl SYSTEM PRIVATE ${_chibi_prefix}/include)
  target_include_directories(boss-repl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
  target_link_libraries(boss-repl
    ${_chibi_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}chibi-scheme${CMAKE_STATIC_LIBRARY_SUFFIX})
  if(UNIX AND NOT APPLE)
    target_link_libraries(boss-repl dl)
  endif()

  # Optional readline support
  find_library(READLINE_LIB readline)
  if(READLINE_LIB)
    target_compile_definitions(boss-repl PRIVATE HAVE_READLINE)
    target_link_libraries(boss-repl ${READLINE_LIB})
  endif()

  set_target_properties(boss-repl PROPERTIES
    INSTALL_RPATH ""
    OUTPUT_NAME boss)

  install(TARGETS boss-repl RUNTIME DESTINATION bin)
endif()


# Tests
add_executable(Tests ${ImplementationFiles} ${TestFiles})
add_dependencies(Tests catch2)

target_link_libraries(Tests ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}Catch2$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX})
if(WIN32)
  target_compile_options(Tests PUBLIC /bigobj)
endif(WIN32)

# Benchmarks
add_executable(Benchmarks EXCLUDE_FROM_ALL ${ImplementationFiles} ${BenchmarkFiles})
target_link_libraries(Benchmarks Threads::Threads)
if(WIN32)
  target_link_libraries(Benchmarks shlwapi.lib)  
endif(WIN32)
add_dependencies(Benchmarks googlebenchmark)
if(ITT_NOTIFY_INCLUDE_DIR)
  message(VERBOSE "found itt notify header in ${ITT_NOTIFY_INCLUDE_DIR}")
  target_include_directories(Benchmarks SYSTEM PUBLIC ${ITT_NOTIFY_INCLUDE_DIR})
  target_link_libraries(Benchmarks ${ITT_NOTIFY_INCLUDE_DIR}/../lib64/libittnotify${CMAKE_STATIC_LIBRARY_SUFFIX})
  if(NOT WIN32)
    target_link_libraries(Benchmarks dl)
  endif(NOT WIN32)
  target_compile_options(Benchmarks PUBLIC -DWITH_ITT_NOTIFY)
endif(ITT_NOTIFY_INCLUDE_DIR)
target_link_libraries(Benchmarks ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark${CMAKE_STATIC_LIBRARY_SUFFIX})

list(APPEND AllTargets BOSS Tests Benchmarks)
if(NOT DISABLE_REPL AND MAKE_FOUND)
  list(APPEND AllTargets boss-repl)
endif()

############################ Targets Common Properties ############################

foreach(Target IN LISTS AllTargets)
  if(NOT WIN32)
    # also using -E option to expose all symbols with the symbol table when dynamically loading the plugins (https://stackoverflow.com/a/9445178)
    if(APPLE)
      target_link_libraries(${Target} "-Wl,-export_dynamic")
    endif()
  else()  
    # for Windows, we need to generate an "import library" for the plugins to link against it
    # since dlls cannot contain any unresolved symbol
    set_target_properties(${Target} PROPERTIES ENABLE_EXPORTS 1)
  endif()

  add_dependencies(${Target} spdlog)
  target_link_libraries(${Target} Threads::Threads)
  set_target_properties(${Target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${Target} PUBLIC "/Zc:__cplusplus")
    target_compile_options(${Target} PUBLIC "/EHsc")
  endif()
  target_include_directories(${Target} SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${Target} rt)
    target_link_libraries(${Target} dl)
  endif()
endforeach()


#################################### Install ###################################
set(PUBLIC_HEADER_LIST
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/BOSS.h;
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/BOSS.hpp;
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine.hpp;
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Expression.hpp;
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/ExpressionUtilities.hpp;
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Utilities.hpp;
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Algorithm.hpp;
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Serialization.hpp;
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/PortableBOSSSerialization.h
)
set_property(TARGET BOSS PROPERTY PUBLIC_HEADER ${PUBLIC_HEADER_LIST})

if(WIN32)
  install(TARGETS BOSS PUBLIC_HEADER DESTINATION include)
  # on Windows, one "import library" for each executable target is needed
  # to link the plugins without leaving any unresolved symbol in the dll
  foreach(Target IN LISTS AllTargets)
    install(FILES ${BOSS_BINARY_DIR}/${Target}.lib DESTINATION lib OPTIONAL)
  endforeach()
else()
  # other OS only need to expose the BOSS library
  install(TARGETS BOSS PUBLIC_HEADER DESTINATION include LIBRARY DESTINATION lib)
endif(WIN32)
install(TARGETS Tests RUNTIME DESTINATION bin)
install(FILES Source/Shims/BOSS.rkt Server/Server.rkt DESTINATION bin)
install(DIRECTORY ${BOSS_BINARY_DIR}/deps/include/spdlog DESTINATION include)

foreach(Engine IN LISTS BOSS_BUILD_ENGINES)
  string(REGEX REPLACE  "([^/]+)/([^:]+):?(.*)" "\\1" Namespace ${Engine})
  string(REGEX REPLACE  "([^/]+)/([^:]+):?(.*)" "\\2" EngineName ${Engine})
  string(REGEX REPLACE  "([^/]+)/([^:]+):?(.*)" "\\3" BranchName ${Engine})
  execute_process(COMMAND git branch --show-current WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE BOSS_CHECKOUT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(BranchName STREQUAL "")
    set(BranchName "main")
  endif()
  ExternalProject_Add(${EngineName}
    GIT_REPOSITORY "https://github.com/${Namespace}/${EngineName}"
    HTTP_HEADER "Authorization: token ${GITHUB_TOKEN}" "Accept: application/octet-stream"
    GIT_TAG ${BranchName}
    DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads/${CMAKE_PROJECT_NAME}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/engines $<IF:$<CONFIG:>,,-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}> -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -DBOSS_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBOSS_SOURCE_REPOSITORY=${CMAKE_CURRENT_SOURCE_DIR} -DBOSS_SOURCE_BRANCH=${BOSS_CHECKOUT_BRANCH}
  )
endforeach()
