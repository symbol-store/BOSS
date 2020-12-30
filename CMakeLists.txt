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

ExternalProject_Add(googlebenchmark
	URL "https://github.com/little-big-h/benchmark/archive/master.zip"
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON -DBENCHMARK_ENABLE_GTEST_TESTS=OFF -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DDCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS} -DBENCHMARK_USE_LIBCXX=YES
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark.a ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main.a
  )

ExternalProject_Add(catch2
	URL https://github.com/catchorg/Catch2/archive/v2.9.2.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCATCH_BUILD_TESTING=NO -DBUILD_TESTING=NO -DCATCH_ENABLE_WERROR=NO -DCATCH_INSTALL_DOCS=NO -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DDCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  )

set(VTune_DIR "" CACHE PATH "Where to look for VTune installation")
find_path(ITT_NOTIFY_INCLUDE_DIR "ittnotify.h" HINTS ${VTune_DIR}/include)

set(CMAKE_MODULE_PATH "${BOSS_SOURCE_DIR}/CMake/Mathematica/CMake/Mathematica" ${CMAKE_MODULE_PATH})
find_package(Mathematica COMPONENTS WSTP)

#################################### Targets ####################################



set(ImplementationFiles Source/BOSS.cpp Source/Engines/Wolfram.cpp)
set(ExecutableFiles Source/main.cpp)
set(TestFiles Tests/BOSSTests.cpp)
set(BenchmarkFiles Benchmarks/BOSSBenchmarks.cpp)
set(SwigFiles Source/BOSS.i)

add_executable(BOSS ${ImplementationFiles} ${ExecutableFiles})
set_property(TARGET BOSS PROPERTY CXX_STANDARD 17)
target_include_directories(BOSS SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)

add_executable(Tests ${ImplementationFiles} ${TestFiles})
set_property(TARGET Tests PROPERTY CXX_STANDARD 17)
target_include_directories(Tests SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
add_dependencies(Tests catch2)


add_executable(Benchmarks ${ImplementationFiles} ${BenchmarkFiles})
set_property(TARGET Benchmarks PROPERTY CXX_STANDARD 17)

list(APPEND AllTargets BOSS Tests Benchmarks)

find_package(SWIG 4) # at least swig 4.0 for C++17 support
find_package(Python3 COMPONENTS Interpreter Development NumPy)
if(NOT Python3_FOUND)
  find_package(Python3 COMPONENTS Interpreter Development)
endif(NOT Python3_FOUND)

if(SWIG_FOUND)
  message(STATUS "Found Swig files: ${SWIG_USE_FILE}")
  INCLUDE(${SWIG_USE_FILE})
  SET(CMAKE_SWIG_FLAGS "-builtin" "-O")
  if(Python3_FOUND)
    message(STATUS "Found Python include dir: ${Python3_INCLUDE_DIRS}")
    set_source_files_properties(${SwigFiles} PROPERTIES CPLUSPLUS ON)
    swig_add_library(BOSS TYPE SHARED LANGUAGE python SOURCES ${SwigFiles} ${ImplementationFiles})
    set_property(TARGET ${SWIG_MODULE_BOSS_REAL_NAME} PROPERTY CXX_STANDARD 17)
    set_property(TARGET ${SWIG_MODULE_BOSS_REAL_NAME} PROPERTY SWIG_USE_TARGET_INCLUDE_DIRECTORIES TRUE)
    target_include_directories(${SWIG_MODULE_BOSS_REAL_NAME} SYSTEM PUBLIC ${Python3_INCLUDE_DIRS})
    target_include_directories(${SWIG_MODULE_BOSS_REAL_NAME} SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${SWIG_MODULE_BOSS_REAL_NAME} SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
    list(APPEND AllTargets ${SWIG_MODULE_BOSS_REAL_NAME})
    if(Python3_NumPy_FOUND)
      message(STATUS "Found Python NumPy include: ${Python3_NumPy_INCLUDE_DIRS}")
      target_include_directories(${SWIG_MODULE_BOSS_REAL_NAME} SYSTEM PUBLIC ${Python3_NumPy_INCLUDE_DIRS})
    else(Python3_NumPy_FOUND)
      message(STATUS "Couldn't find NumPy. Try executing Python code to find it...")
      execute_process(COMMAND "${Python3_EXECUTABLE}" "-c"
        "import numpy; print(numpy.get_include());"
        RESULT_VARIABLE __result_numpy
        OUTPUT_VARIABLE __output_numpy
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(${__result_numpy} EQUAL 0)
        message(STATUS "Found Python NumPy include: ${__output_numpy}")
        target_include_directories(${SWIG_MODULE_BOSS_REAL_NAME} SYSTEM PUBLIC ${__output_numpy})
      endif(${__result_numpy} EQUAL 0)
    endif(Python3_NumPy_FOUND)
    if("${Python3_LIBRARIES}" STREQUAL "")
      message(STATUS "Couldn't find Python3_LIBRARY. Try executing python3-config to find the ldflags...")
      execute_process(COMMAND "${Python3_EXECUTABLE}-config" "--ldflags"
        RESULT_VARIABLE __result_python3_ldflags
        OUTPUT_VARIABLE __output_python3_ldflags
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(${__result_python3_ldflags} EQUAL 0)
        message(STATUS "Found Python3 ldflags: ${__output_python3_ldflags}")
        target_link_options(${SWIG_MODULE_BOSS_REAL_NAME} BEFORE PUBLIC ${__output_python3_ldflags})
      endif(${__result_python3_ldflags} EQUAL 0)
    else("${Python3_LIBRARIES}" STREQUAL "")
      message(STATUS "Found Python libraries: ${Python3_LIBRARIES}")
    endif("${Python3_LIBRARIES}" STREQUAL "")
    target_link_libraries(${SWIG_MODULE_BOSS_REAL_NAME} ${Python3_LIBRARIES})
    message(STATUS "Swig Python module: ${SWIG_MODULE_BOSS_REAL_NAME}")
  endif(Python3_FOUND)
endif(SWIG_FOUND)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(Benchmarks Threads::Threads)

add_dependencies(Benchmarks googlebenchmark)
target_include_directories(Benchmarks SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
if(ITT_NOTIFY_INCLUDE_DIR)
  message(STATUS "found itt notify header in ${ITT_NOTIFY_INCLUDE_DIR}")
  target_include_directories(Benchmarks SYSTEM PUBLIC ${ITT_NOTIFY_INCLUDE_DIR})
  target_link_libraries(Benchmarks ${ITT_NOTIFY_INCLUDE_DIR}/../lib64/libittnotify.a)
  target_link_libraries(Benchmarks dl)
  target_compile_options(Benchmarks PUBLIC -DWITH_ITT_NOTIFY)
endif(ITT_NOTIFY_INCLUDE_DIR)
target_link_libraries(Benchmarks ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark.a)
target_link_libraries(Benchmarks ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main.a)


foreach (Target IN LISTS AllTargets)
  if(Mathematica_WSTP_FOUND)
    target_include_directories(${Target} PUBLIC ${Mathematica_INCLUDE_DIRS})
    target_link_libraries(${Target} ${Mathematica_WSTP_LIBRARY})
    target_compile_definitions(${Target} PUBLIC "${Mathematica_WSTP_DEFINITIONS}" "-DMATHEMATICA_KERNEL_EXECUTABLE=${Mathematica_KERNEL_EXECUTABLE}")
  endif(Mathematica_WSTP_FOUND)
endforeach()

