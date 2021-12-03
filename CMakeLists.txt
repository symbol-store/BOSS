cmake_minimum_required(VERSION 3.10)
project(BOSS)

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

if("${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")
   set(ARROW_BUILD_TYPE "Release")
 elseif("${CMAKE_BUILD_TYPE}" STREQUAL "Sanitize")
   set(ARROW_BUILD_TYPE "Debug")
 else()
   set(ARROW_BUILD_TYPE ${CMAKE_BUILD_TYPE})
 endif()

if(WIN32)
   set(ARROW_STATIC_LIB_NAME "arrow_static")
   set(ARROW_ADDITIONAL_CMAKE_ARGS -DARROW_MIMALLOC=ON)
   # use arrow_bundled_dependencies only if MIMALLOC is ON
   set(ARROW_BUNDLED_DEPENDENCIES "arrow_bundled_dependencies")
 else()
   set(ARROW_STATIC_LIB_NAME "arrow")
   set(ARROW_ADDITIONAL_CMAKE_ARGS -DARROW_JEMALLOC=ON)
   # use arrow_bundled_dependencies only if JEMALLOC is ON
   set(ARROW_BUNDLED_DEPENDENCIES "arrow_bundled_dependencies")
 endif(WIN32)

 ExternalProject_Add(arrow
   URL https://github.com/apache/arrow/archive/apache-arrow-6.0.0.zip
   SOURCE_SUBDIR cpp
   DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
   CMAKE_ARGS -DARROW_BUILD_SHARED=OFF -DARROW_BUILD_STATIC=ON -DARROW_JSON=ON -DARROW_CSV=ON ${ARROW_ADDITIONAL_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps $<IF:$<CONFIG:>,-DCMAKE_BUILD_TYPE=Debug,-DCMAKE_BUILD_TYPE=${ARROW_BUILD_TYPE}> -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS} -DCMAKE_LINKER=${CMAKE_LINKER} -DARROW_ENABLE_TIMING_TESTS=OFF -DARROW_DEPENDENCY_USE_SHARED=OFF -DARROW_ALTIVEC=OFF -DARROW_BOOST_USE_SHARED=OFF -DARROW_PROTOBUF_USE_SHARED=OFF -DARROW_THRIFT_USE_SHARED=OFF
   BUILD_BYPRODUCTS  ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${ARROW_STATIC_LIB_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${ARROW_BUNDLED_DEPENDENCIES}${CMAKE_STATIC_LIBRARY_SUFFIX}
 )

# on Windows, expose all the arrow symbols with a .def file to specify the symbols to include in the executable library
if(WIN32)
  find_program(POWERSHELL_PATH NAMES powershell)
  # get the list of all the Arrow .obj files
  ExternalProject_Add_Step(arrow generateDefObjsList DEPENDEES install
      WORKING_DIRECTORY <BINARY_DIR>
      BYPRODUCTS arrow.def.objs
      COMMAND ${POWERSHELL_PATH} "get-childitem src/arrow/CMakeFiles/arrow_static.dir -recurse | where {$_.extension -eq '.obj'} | % {Write-Output $_.FullName} | Resolve-Path -Relative | % {Write-Output $_.Substring(2)} | out-file -encoding ASCII  -filepath arrow.def.objs")
  # inspect the symbols and them to the Arrow .def file
  ExternalProject_Add_Step(arrow generateDef
      WORKING_DIRECTORY <BINARY_DIR>
      DEPENDS arrow.def.objs
      BYPRODUCTS arrow.def
      COMMAND ${CMAKE_COMMAND} -E rm -f arrow.def && ${CMAKE_COMMAND} -E  __create_def arrow.def arrow.def.objs)
  # install the def file with the other Arrow dependencies
  ExternalProject_Add_Step(arrow installDef
      WORKING_DIRECTORY <BINARY_DIR>
      DEPENDS arrow.def
      BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/arrow.def
      COMMAND ${CMAKE_COMMAND} -E copy arrow.def ${BOSS_BINARY_DIR}/deps/lib/arrow.def)
  set(ARROW_DEF_FILE ${BOSS_BINARY_DIR}/deps/lib/arrow.def)
endif(WIN32)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(GOOGLEBENCHMARK_USE_LIBCXX YES CACHE BOOL "build google benchmark with libc++")
else()
  set(GOOGLEBENCHMARK_USE_LIBCXX NO CACHE BOOL "build google benchmark with libc++")
endif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")


ExternalProject_Add(googlebenchmark
	URL "https://github.com/little-big-h/benchmark/archive/master.zip"
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON -DBENCHMARK_ENABLE_GTEST_TESTS=OFF $<IF:$<CONFIG:>,,-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}> -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark${CMAKE_STATIC_LIBRARY_SUFFIX} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main${CMAKE_STATIC_LIBRARY_SUFFIX}
  )

ExternalProject_Add(catch2
	URL https://github.com/catchorg/Catch2/archive/v2.13.6.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCATCH_BUILD_TESTING=NO -DBUILD_TESTING=NO -DCATCH_ENABLE_WERROR=NO -DCATCH_INSTALL_DOCS=NO -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  )


ExternalProject_Add(spdlog
	URL https://github.com/gabime/spdlog/archive/refs/tags/v1.8.5.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  )

# MonetDB (for benchmarking)
if(WIN32)
    set(MONETDB_SHARED_LIB_EXTENSION ${CMAKE_LINK_LIBRARY_SUFFIX})
else()
    set(MONETDB_SHARED_LIB_EXTENSION ${CMAKE_SHARED_LIBRARY_SUFFIX})
endif(WIN32)
ExternalProject_Add(monetdb
  GIT_REPOSITORY https://github.com/MonetDB/MonetDBLite-C
  GIT_TAG master
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} 
    -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS} -DCMAKE_LINKER=${CMAKE_LINKER}
  BUILD_COMMAND "${CMAKE_COMMAND}" --build . --target libmonetdb5
  INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/src/embedded
                  ${BOSS_BINARY_DIR}/deps/include/embedded
  COMMAND ${CMAKE_COMMAND} -E copy <BINARY_DIR>/${CMAKE_SHARED_LIBRARY_PREFIX}libmonetdb5${MONETDB_SHARED_LIB_EXTENSION}
          ${BOSS_BINARY_DIR}/deps/lib/
  COMMAND ${CMAKE_COMMAND} -E copy <BINARY_DIR>/${CMAKE_SHARED_LIBRARY_PREFIX}libmonetdb5${CMAKE_SHARED_LIBRARY_SUFFIX}
          ${BOSS_BINARY_DIR}/
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}libmonetdb5${MONETDB_SHARED_LIB_EXTENSION}
)

# DuckDB (for benchmarking)
if(WIN32)
    set(DUCKDB_SHARED_LIB_EXTENSION ${CMAKE_LINK_LIBRARY_SUFFIX})
else()
    set(DUCKDB_SHARED_LIB_EXTENSION ${CMAKE_SHARED_LIBRARY_SUFFIX})
endif(WIN32)
ExternalProject_Add(duckdblib
  GIT_REPOSITORY https://github.com/duckdb/duckdb.git
  GIT_TAG v0.2.9
  CMAKE_ARGS -DBUILD_SHELL=FALSE -DBUILD_UNITTESTS=FALSE -DBUILD_TPCH_EXTENSION=FALSE -DENABLE_SANITIZER=FALSE -DENABLE_UBSAN=FALSE
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} 
    -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS} -DCMAKE_LINKER=${CMAKE_LINKER}
  INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/src/include
                  ${BOSS_BINARY_DIR}/deps/include
  COMMAND ${CMAKE_COMMAND} -E copy <BINARY_DIR>/src/${CMAKE_SHARED_LIBRARY_PREFIX}duckdb${DUCKDB_SHARED_LIB_EXTENSION}
          ${BOSS_BINARY_DIR}/deps/lib/
  COMMAND ${CMAKE_COMMAND} -E copy <BINARY_DIR>/src/${CMAKE_SHARED_LIBRARY_PREFIX}duckdb${CMAKE_SHARED_LIBRARY_SUFFIX}
          ${BOSS_BINARY_DIR}/
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}duckdb${DUCKDB_SHARED_LIB_EXTENSION}
)

########################## Other Library Dependencies ##########################

set(VTune_DIR "" CACHE PATH "Where to look for VTune installation")
find_path(ITT_NOTIFY_INCLUDE_DIR "ittnotify.h" HINTS ${VTune_DIR}/include)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

############################### Executable Targets ###############################

set(ImplementationFiles Source/BOSS.cpp ${ARROW_DEF_FILE})
set(TestFiles Tests/BOSSTests.cpp Tests/CAPITests.cpp ${ARROW_DEF_FILE})
set(BenchmarkFiles Benchmarks/BOSSBenchmarks.cpp ${ARROW_DEF_FILE})

# BOSS
add_library(BOSS SHARED ${ImplementationFiles})

# Tests
add_executable(Tests ${ImplementationFiles} ${TestFiles})
add_dependencies(Tests catch2)

# Benchmarks
add_executable(Benchmarks ${ImplementationFiles} ${BenchmarkFiles})
target_link_libraries(Benchmarks Threads::Threads)
if(WIN32)
  target_link_libraries(Benchmarks shlwapi.lib)  
endif(WIN32)
add_dependencies(Benchmarks googlebenchmark)
add_dependencies(Benchmarks monetdb duckdblib)
target_link_libraries(Benchmarks libmonetdb5 duckdb)
if(ITT_NOTIFY_INCLUDE_DIR)
  message(VERBOSE "found itt notify header in ${ITT_NOTIFY_INCLUDE_DIR}")
  target_include_directories(Benchmarks SYSTEM PUBLIC ${ITT_NOTIFY_INCLUDE_DIR})
  target_link_libraries(Benchmarks ${ITT_NOTIFY_INCLUDE_DIR}/../lib64/libittnotify${CMAKE_STATIC_LIBRARY_SUFFIX})
  if(NOT WIN32)
    target_link_libraries(Benchmarks dl)
  endif(NOT WIN32)
  target_compile_options(Benchmarks PUBLIC -DWITH_ITT_NOTIFY)
endif(ITT_NOTIFY_INCLUDE_DIR)
target_link_libraries(Benchmarks benchmark)
target_link_libraries(Benchmarks benchmark_main)

list(APPEND AllTargets BOSS Tests Benchmarks)

############################ Targets Common Properties ############################

foreach(Target IN LISTS AllTargets)
  add_dependencies(${Target} arrow)
  target_compile_options(${Target} PUBLIC -DARROW_STATIC)
  set(ARROW_LIBRARY_FILE ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${ARROW_STATIC_LIB_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX})
  if(ARROW_BUNDLED_DEPENDENCIES)
    set(ARROW_ADDITIONAL_LIBRARY_FILE ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${ARROW_BUNDLED_DEPENDENCIES}${CMAKE_STATIC_LIBRARY_SUFFIX})
  endif(ARROW_BUNDLED_DEPENDENCIES)
  # make sure to add all symbols when linking with Arrow library (potentially used by plugins)
  # also using -E option to expose all symbols with the symbol table when dynamically loading the plugins (https://stackoverflow.com/a/9445178)
  if(WIN32)
    target_link_options(${Target} PUBLIC /WHOLEARCHIVE:${ARROW_LIBRARY_FILE})
    if(ARROW_ADDITIONAL_LIBRARY_FILE)
	  target_link_options(${Target} PUBLIC /WHOLEARCHIVE:${ARROW_ADDITIONAL_LIBRARY_FILE})
    endif(ARROW_ADDITIONAL_LIBRARY_FILE)
    # for Windows, we need to generate an "import library" for the plugins to link against it
    # since dlls cannot contain any unresolved symbol
    set_target_properties(${Target} PROPERTIES ENABLE_EXPORTS 1)
  elseif(APPLE)
    target_link_options(${Target} PUBLIC -Wl,-E -Wl,-all_load ${ARROW_LIBRARY_FILE} -Wl,-noall_load)
	if(ARROW_ADDITIONAL_LIBRARY_FILE)
	  target_link_options(${Target} PUBLIC -Wl,-all_load ${ARROW_ADDITIONAL_LIBRARY_FILE} -Wl,-noall_load)
    endif(ARROW_ADDITIONAL_LIBRARY_FILE)
  else()
    target_link_options(${Target} PUBLIC -Wl,-E -Wl,--whole-archive ${ARROW_LIBRARY_FILE} -Wl,--no-whole-archive)
	if(ARROW_ADDITIONAL_LIBRARY_FILE)
      target_link_options(${Target} PUBLIC -Wl,--whole-archive ${ARROW_ADDITIONAL_LIBRARY_FILE} -Wl,--no-whole-archive)
    endif(ARROW_ADDITIONAL_LIBRARY_FILE)
  endif()
  add_dependencies(${Target} spdlog)
  target_link_libraries(${Target} Threads::Threads)
  set_target_properties(${Target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${Target} PUBLIC "/Zc:__cplusplus")
    target_compile_options(${Target} PUBLIC "/EHsc")
  endif()
  target_include_directories(${Target} SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
  target_link_directories(${Target} PUBLIC ${BOSS_BINARY_DIR}/deps/lib)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${Target} rt)
    target_link_libraries(${Target} dl)
  endif() 
endforeach()


#################################### Install ###################################
set_target_properties(BOSS PROPERTIES PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Source/BOSS.hpp;${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine.hpp;${CMAKE_CURRENT_SOURCE_DIR}/Source/Expression.hpp;${CMAKE_CURRENT_SOURCE_DIR}/Source/ExpressionUtilities.hpp;${CMAKE_CURRENT_SOURCE_DIR}/Source/Utilities.hpp")

install(TARGETS BOSS PUBLIC_HEADER DESTINATION include)
if(WIN32)
  # on Windows, one "import library" for each executable target is needed
  # to link the plugins without leaving any unresolved symbol in the dll
  foreach(Target IN LISTS AllTargets)
    install(FILES ${BOSS_BINARY_DIR}/${Target}.lib DESTINATION lib)
  endforeach()
else()
  # other OS only need to expose the BOSS library
  install(TARGETS BOSS LIBRARY DESTINATION lib)
endif(WIN32)
install(FILES ${BOSS_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}libmonetdb5${CMAKE_SHARED_LIBRARY_SUFFIX} DESTINATION bin)
install(FILES ${BOSS_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}duckdb${CMAKE_SHARED_LIBRARY_SUFFIX} DESTINATION bin)
install(TARGETS Tests RUNTIME DESTINATION bin)
install(TARGETS Benchmarks RUNTIME DESTINATION bin)
install(FILES Source/Shims/BOSS.rkt Server/Server.rkt DESTINATION bin)
install(DIRECTORY ${BOSS_BINARY_DIR}/deps/include/spdlog DESTINATION include)
install(DIRECTORY ${BOSS_BINARY_DIR}/deps/include/arrow DESTINATION include)




