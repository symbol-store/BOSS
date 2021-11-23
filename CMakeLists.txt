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

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(GOOGLEBENCHMARK_USE_LIBCXX YES CACHE BOOL "build google benchmark with libc++")
else()
  set(GOOGLEBENCHMARK_USE_LIBCXX NO CACHE BOOL "build google benchmark with libc++")
endif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")


ExternalProject_Add(googlebenchmark
	URL "https://github.com/little-big-h/benchmark/archive/master.zip"
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON -DBENCHMARK_ENABLE_GTEST_TESTS=OFF $<IF:$<CONFIG:>,,-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}> -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DDCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark${CMAKE_STATIC_LIBRARY_SUFFIX} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main${CMAKE_STATIC_LIBRARY_SUFFIX}
  )

ExternalProject_Add(catch2
	URL https://github.com/catchorg/Catch2/archive/v2.13.6.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCATCH_BUILD_TESTING=NO -DBUILD_TESTING=NO -DCATCH_ENABLE_WERROR=NO -DCATCH_INSTALL_DOCS=NO -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DDCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  )


ExternalProject_Add(spdlog
	URL https://github.com/gabime/spdlog/archive/refs/tags/v1.8.5.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DDCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  )

########################## Other Library Dependencies ##########################

set(VTune_DIR "" CACHE PATH "Where to look for VTune installation")
find_path(ITT_NOTIFY_INCLUDE_DIR "ittnotify.h" HINTS ${VTune_DIR}/include)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


############################### Executable Targets ###############################

set(ImplementationFiles Source/BOSS.cpp)
set(TestFiles Tests/BOSSTests.cpp Tests/CAPITests.cpp)
set(BenchmarkFiles Benchmarks/BOSSBenchmarks.cpp)

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
target_link_libraries(Benchmarks ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main${CMAKE_STATIC_LIBRARY_SUFFIX})

list(APPEND AllTargets BOSS Tests Benchmarks)

############################ Targets Common Properties ############################

foreach (Target IN LISTS AllTargets)
  add_dependencies(${Target} arrow)
  target_compile_options(${Target} PUBLIC -DARROW_STATIC)
  target_link_libraries(${Target} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${ARROW_STATIC_LIB_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX})
  if(ARROW_BUNDLED_DEPENDENCIES)
    target_link_libraries(${Target} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${ARROW_BUNDLED_DEPENDENCIES}${CMAKE_STATIC_LIBRARY_SUFFIX})
  endif(ARROW_BUNDLED_DEPENDENCIES)

  add_dependencies(${Target} spdlog)
  target_link_libraries(${Target} Threads::Threads)
  set_target_properties(${Target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${Target} PUBLIC "/Zc:__cplusplus")
  endif()
  target_include_directories(${Target} SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${Target} rt)
    target_link_libraries(${Target} dl)
  endif() 
endforeach()


#################################### Install ###################################
set_target_properties(BOSS PROPERTIES PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Source/BOSS.hpp;${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine.hpp;${CMAKE_CURRENT_SOURCE_DIR}/Source/Expression.hpp;${CMAKE_CURRENT_SOURCE_DIR}/Source/ExpressionUtilities.hpp;${CMAKE_CURRENT_SOURCE_DIR}/Source/Utilities.hpp")

install(TARGETS BOSS LIBRARY DESTINATION lib PUBLIC_HEADER DESTINATION include)
install(TARGETS Tests RUNTIME DESTINATION bin)
install(FILES Source/Shims/BOSS.rkt Server/Server.rkt DESTINATION bin)
install(DIRECTORY ${BOSS_BINARY_DIR}/deps/include/spdlog DESTINATION include)
install(DIRECTORY ${BOSS_BINARY_DIR}/deps/include/arrow DESTINATION include)




