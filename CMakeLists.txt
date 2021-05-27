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

if(WIN32)
  set(GOOGLEBENCHMARK_USE_LIBCXX NO CACHE BOOL "build google benchmark with libc++")
else()
  set(GOOGLEBENCHMARK_USE_LIBCXX YES CACHE BOOL "build google benchmark with libc++")
endif(WIN32)

ExternalProject_Add(googlebenchmark
	URL "https://github.com/little-big-h/benchmark/archive/master.zip"
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON -DBENCHMARK_ENABLE_GTEST_TESTS=OFF -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DDCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS} -DBENCHMARK_USE_LIBCXX=${GOOGLEBENCHMARK_USE_LIBCXX}
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark${CMAKE_STATIC_LIBRARY_SUFFIX} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main${CMAKE_STATIC_LIBRARY_SUFFIX}
  )

ExternalProject_Add(catch2
	URL https://github.com/catchorg/Catch2/archive/v2.13.6.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCATCH_BUILD_TESTING=NO -DBUILD_TESTING=NO -DCATCH_ENABLE_WERROR=NO -DCATCH_INSTALL_DOCS=NO -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DDCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  )

########################## Other Library Dependencies ##########################

set(VTune_DIR "" CACHE PATH "Where to look for VTune installation")
find_path(ITT_NOTIFY_INCLUDE_DIR "ittnotify.h" HINTS ${VTune_DIR}/include)

set(CMAKE_MODULE_PATH "${BOSS_SOURCE_DIR}/CMake/Mathematica/CMake/Mathematica" ${CMAKE_MODULE_PATH})
find_package(Mathematica COMPONENTS WSTP)

find_package(SWIG 3)

set(Racket_ROOT "" CACHE PATH "Where to look for Racket API")
find_path(Racket_LIB_DIR "mzdyn3m${CMAKE_CXX_OUTPUT_EXTENSION}" HINTS ${Racket_ROOT}/lib ${Racket_ROOT}/lib/racket ${Racket_ROOT}/lib/msvc)
find_path(Racket_INCLUDE_DIR "escheme.h" HINTS ${Racket_ROOT}/include ${Racket_ROOT}/include/racket)

find_package(Python3 COMPONENTS Interpreter Development NumPy)
if(NOT Python3_FOUND)
  find_package(Python3 COMPONENTS Interpreter Development)
endif(NOT Python3_FOUND)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#################################### Backends ####################################

# Wolfram BOSS
add_library(WolframBOSS STATIC Source/Engines/Wolfram.cpp ${ImplementationFiles})
if(Mathematica_WSTP_FOUND)
  target_include_directories(WolframBOSS PUBLIC ${Mathematica_INCLUDE_DIRS})
  target_link_libraries(WolframBOSS ${Mathematica_WSTP_LIBRARY})
  if(UNIX AND NOT APPLE)
    target_link_libraries(WolframBOSS uuid)
  endif()
  if(NOT WIN32)
    target_link_libraries(WolframBOSS dl)
  endif(NOT WIN32)
  target_compile_definitions(WolframBOSS PUBLIC "${Mathematica_WSTP_DEFINITIONS}" "-DMATHEMATICA_KERNEL_EXECUTABLE=${Mathematica_KERNEL_EXECUTABLE}")
  if(WIN32)
    add_custom_command(TARGET WolframBOSS POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Mathematica_WSTP_RUNTIME_LIBRARY_DIRS}/wstp64i4.dll"  $<TARGET_FILE_DIR:WolframBOSS>)
  endif(WIN32)
endif(Mathematica_WSTP_FOUND)

list(APPEND AllBackends WolframBOSS)

############################ Backends Common Properties ############################

foreach (Backend IN LISTS AllBackends)
  target_link_libraries(${Backend} Threads::Threads)
  set_target_properties(${Backend} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  set_target_properties(${Backend} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(${Backend} SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${Backend} rt)
  endif()  
endforeach()

############################### Executable Targets ###############################

set(ImplementationFiles Source/BOSS.cpp Source/Expression.cpp)
set(ExecutableFiles Source/main.cpp)
set(TestFiles Tests/BOSSTests.cpp)
set(BenchmarkFiles Benchmarks/BOSSBenchmarks.cpp)

# BOSS
add_executable(BOSS ${ImplementationFiles} ${ExecutableFiles})

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

################################# Swig Bindings #################################

set(SwigFiles Source/BOSS.i)

if(SWIG_FOUND)
  message(VERBOSE "Found Swig files: ${SWIG_USE_FILE}")
  INCLUDE(${SWIG_USE_FILE})
  set_source_files_properties(${SwigFiles} PROPERTIES CPLUSPLUS ON)

  if(Racket_LIB_DIR AND Racket_INCLUDE_DIR)
    message(VERBOSE "Found Racket lib dir: ${Racket_LIB_DIR}")
    message(VERBOSE "Found Racket include dir: ${Racket_INCLUDE_DIR}")
    swig_add_library(RacketBOSS TYPE MODULE LANGUAGE mzscheme SOURCES ${SwigFiles} ${ImplementationFiles} "${Racket_LIB_DIR}/mzdyn3m${CMAKE_CXX_OUTPUT_EXTENSION}")
    set_property(TARGET ${SWIG_MODULE_RacketBOSS_REAL_NAME} PROPERTY CXX_CLANG_TIDY "") # no clang-tidy
    target_compile_options(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC -DMZ_PRECISE_GC)
    if(APPLE)
      target_compile_options(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC -DOS_X)
      target_link_options(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC  -bundle -flat_namespace -undefined suppress)
    endif(APPLE)
    if(NOT WIN32)
      target_compile_options(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC -Wno-reserved-user-defined-literal)
    endif(NOT WIN32)
    set_property(TARGET ${SWIG_MODULE_RacketBOSS_REAL_NAME} PROPERTY SWIG_USE_TARGET_INCLUDE_DIRECTORIES TRUE)
    target_include_directories(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC "${Racket_INCLUDE_DIR}")
    list(APPEND AllTargets ${SWIG_MODULE_RacketBOSS_REAL_NAME})
  endif(Racket_LIB_DIR AND Racket_INCLUDE_DIR)

  if(Python3_FOUND)
    message(VERBOSE "Found Python include dir: ${Python3_INCLUDE_DIRS}")
    set_property(SOURCE ${SwigFiles} PROPERTY COMPILE_OPTIONS "-builtin;-O")
    swig_add_library(PyBOSS TYPE SHARED LANGUAGE python SOURCES ${SwigFiles} ${ImplementationFiles})
    set_property(TARGET ${SWIG_MODULE_PyBOSS_REAL_NAME} PROPERTY SWIG_USE_TARGET_INCLUDE_DIRECTORIES TRUE)
    target_include_directories(${SWIG_MODULE_PyBOSS_REAL_NAME} SYSTEM PUBLIC ${Python3_INCLUDE_DIRS})
    target_include_directories(${SWIG_MODULE_PyBOSS_REAL_NAME} SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET ${SWIG_MODULE_PyBOSS_REAL_NAME} PROPERTY CXX_CLANG_TIDY "") # no clang-tidy
    list(APPEND AllTargets ${SWIG_MODULE_PyBOSS_REAL_NAME})
    if(Python3_NumPy_FOUND)
      message(VERBOSE "Found Python NumPy include: ${Python3_NumPy_INCLUDE_DIRS}")
      target_include_directories(${SWIG_MODULE_PyBOSS_REAL_NAME} SYSTEM PUBLIC ${Python3_NumPy_INCLUDE_DIRS})
    else(Python3_NumPy_FOUND)
      message(VERBOSE "Couldn't find NumPy. Try executing Python code to find it...")
      execute_process(COMMAND "${Python3_EXECUTABLE}" "-c"
        "import numpy; print(numpy.get_include());"
        RESULT_VARIABLE __result_numpy
        OUTPUT_VARIABLE __output_numpy
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(${__result_numpy} EQUAL 0)
        message(VERBOSE "Found Python NumPy include: ${__output_numpy}")
        target_include_directories(${SWIG_MODULE_PyBOSS_REAL_NAME} SYSTEM PUBLIC ${__output_numpy})
      endif(${__result_numpy} EQUAL 0)
    endif(Python3_NumPy_FOUND)
    if("${Python3_LIBRARIES}" STREQUAL "")
      message(VERBOSE "Couldn't find Python3_LIBRARY. Try executing python3-config to find the ldflags...")
      execute_process(COMMAND "${Python3_EXECUTABLE}-config" "--ldflags"
        RESULT_VARIABLE __result_python3_ldflags
        OUTPUT_VARIABLE __output_python3_ldflags
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(${__result_python3_ldflags} EQUAL 0)
        message(VERBOSE "Found Python3 ldflags: ${__output_python3_ldflags}")
        target_link_options(${SWIG_MODULE_PyBOSS_REAL_NAME} BEFORE PUBLIC ${__output_python3_ldflags})
      endif(${__result_python3_ldflags} EQUAL 0)
    else("${Python3_LIBRARIES}" STREQUAL "")
      message(VERBOSE "Found Python libraries: ${Python3_LIBRARIES}")
    endif("${Python3_LIBRARIES}" STREQUAL "")
    target_link_libraries(${SWIG_MODULE_PyBOSS_REAL_NAME} ${Python3_LIBRARIES})
    message(VERBOSE "Swig Python module: ${SWIG_MODULE_PyBOSS_REAL_NAME}")
  endif(Python3_FOUND)
endif(SWIG_FOUND)

############################ Targets Common Properties ############################

foreach (Target IN LISTS AllTargets)
  target_link_libraries(${Target} Threads::Threads)
  set_target_properties(${Target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(${Target} SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${Target} rt)
  endif() 
  # Link backends to targets
  foreach (Target IN LISTS AllTargets)
    foreach (Backend IN LISTS AllBackends)
      target_link_libraries(${Target} ${Backend})
    endforeach()
  endforeach()
endforeach()

