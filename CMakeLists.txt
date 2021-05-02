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
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON -DBENCHMARK_ENABLE_GTEST_TESTS=OFF -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS} -DBENCHMARK_USE_LIBCXX=YES
  BUILD_BYPRODUCTS ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark.a ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main.a
  )

ExternalProject_Add(catch2
	URL https://github.com/catchorg/Catch2/archive/v2.9.2.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCATCH_BUILD_TESTING=NO -DBUILD_TESTING=NO -DCATCH_ENABLE_WERROR=NO -DCATCH_INSTALL_DOCS=NO -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS}
  )

ExternalProject_Add(arrow
  URL https://github.com/apache/arrow/archive/apache-arrow-3.0.0.zip
  SOURCE_SUBDIR cpp
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${BOSS_BINARY_DIR}/deps -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${DCMAKE_CXX_FLAGS} -DCMAKE_C_FLAGS=${DCMAKE_C_FLAGS} -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS} -DCMAKE_LINKER=${CMAKE_LINKER}
)

# VTune
set(VTune_DIR "" CACHE PATH "Where to look for VTune installation")
find_path(ITT_NOTIFY_INCLUDE_DIR "ittnotify.h" HINTS ${VTune_DIR}/include)

# Mathematica
set(CMAKE_MODULE_PATH "${BOSS_SOURCE_DIR}/CMake/Mathematica/CMake/Mathematica" ${CMAKE_MODULE_PATH})
find_package(Mathematica COMPONENTS WSTP)

# =============== LLVM/MLIR Config =============
find_package(MLIR REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${PROJECT_BINARY_DIR}/Source/Engines/MLIREngine/Dialect/SExprDialect)
include_directories(${PROJECT_BINARY_DIR}/Source/Engines/MLIREngine/Dialect/MemoryDialect)
include_directories(${PROJECT_BINARY_DIR}/Source/Engines/MLIREngine/Dialect/DatabaseDialect)
include_directories(${PROJECT_BINARY_DIR}/Source/Engines/MLIREngine/Dialect)
include_directories(${PROJECT_BINARY_DIR}/Source/Engines/MLIREngine/Translation)
link_directories(${LLVM_BUILD_LIBRARY_DIR})
add_definitions(${LLVM_DEFINITIONS})

set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/lib)
set(MLIR_BINARY_DIR ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

set(LLVM_ENABLE_EH ON)
set(LLVM_ENABLE_RTTI ON)
# ================ End LLVM/MLIR Config =========

set(Racket_ROOT "" CACHE PATH "Where to look for Racket API")
find_path(Racket_LIB_DIR "mzdyn3m.o" HINTS ${Racket_ROOT}/lib ${Racket_ROOT}/lib/racket)
find_path(Racket_INCLUDE_DIR "escheme.h" HINTS ${Racket_ROOT}/include ${Racket_ROOT}/include/racket)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#################################### Targets ####################################

# ========= MLIR Egine Source Files =============
# MLIR Engine Implementation Files
set(MLIREngineImplFiles
  Source/Engines/MLIREngine/Analysis/TypeInferencePass.cpp
  Source/Engines/MLIREngine/IR/MLIRGenerator.cpp
  Source/Engines/MLIREngine.cpp
  Source/Engines/MLIREngine/Runtime/Runtime.cpp
  Source/Engines/MLIREngine/Types/TypeConversions.cpp
  Source/Engines/MLIREngine/Types/ValueConversion.cpp
  Source/Engines/MLIREngine/Types/TypeInference.cpp
  Source/Engines/MLIREngine/Runtime/Storage.cpp
)

# Get MLIR dialect and conversion libraries
include_directories(Source)
add_subdirectory(Source/Engines/MLIREngine/Dialect)
add_subdirectory(Source/Engines/MLIREngine/Translation)
get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)
get_property(conversion_libs GLOBAL PROPERTY MLIR_CONVERSION_LIBS)
get_property(interface_libs GLOBAL PROPERTY MLIR_INTERFACES)
# ========= End MLIR Egine Source Files ==========

set(ImplementationFiles Source/BOSS.cpp Source/Engines/Wolfram.cpp ${MLIREngineImplFiles})
set(ExecutableFiles Source/main.cpp)
set(TestFiles Tests/BOSSTests.cpp Tests/StorageTests.cpp)
set(BenchmarkFiles Benchmarks/BOSSBenchmarks.cpp)
set(SwigFiles Source/BOSS.i)

# llvm_update_compile_flags(BOSS LLVM_REQUIRES_EH ON)
add_executable(BOSS ${ImplementationFiles} ${ExecutableFiles})
set_property(TARGET BOSS PROPERTY CXX_STANDARD 17)
target_include_directories(BOSS SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)

add_executable(Tests ${ImplementationFiles} ${TestFiles})
add_dependencies(Tests catch2 arrow)

add_executable(Benchmarks ${ImplementationFiles} ${BenchmarkFiles})
set_property(TARGET Benchmarks PROPERTY CXX_STANDARD 17)

list(APPEND AllTargets BOSS Tests Benchmarks)

find_package(SWIG 4) # at least swig 4.0 for C++17 support

if(SWIG_FOUND)
  message(VERBOSE "Found Swig files: ${SWIG_USE_FILE}")
  INCLUDE(${SWIG_USE_FILE})
  set_source_files_properties(${SwigFiles} PROPERTIES CPLUSPLUS ON)

  if(Racket_LIB_DIR AND Racket_INCLUDE_DIR)
    message(VERBOSE "Found Racket lib dir: ${Racket_LIB_DIR}")
    message(VERBOSE "Found Racket include dir: ${Racket_INCLUDE_DIR}")
    set_property(SOURCE ${SwigFiles} PROPERTY COMPILE_OPTIONS "")
    swig_add_library(RacketBOSS TYPE MODULE LANGUAGE mzscheme SOURCES ${SwigFiles} ${ImplementationFiles} "${Racket_LIB_DIR}/mzdyn3m.o")
    add_dependencies(${SWIG_MODULE_RacketBOSS_REAL_NAME} arrow)
    set_property(TARGET ${SWIG_MODULE_RacketBOSS_REAL_NAME} PROPERTY CXX_STANDARD 17)
    target_compile_options(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC -DMZ_PRECISE_GC)
    if(APPLE)
      target_compile_options(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC -DOS_X)
    endif(APPLE)
    target_compile_options(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC -Wno-reserved-user-defined-literal)
    target_link_options(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC  -bundle -flat_namespace -undefined suppress)
    set_property(TARGET ${SWIG_MODULE_RacketBOSS_REAL_NAME} PROPERTY SWIG_USE_TARGET_INCLUDE_DIRECTORIES TRUE)
    target_include_directories(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${SWIG_MODULE_RacketBOSS_REAL_NAME} PUBLIC "${Racket_INCLUDE_DIR}")
    list(APPEND AllTargets ${SWIG_MODULE_RacketBOSS_REAL_NAME})
  endif(Racket_LIB_DIR AND Racket_INCLUDE_DIR)

  find_package(Python3 COMPONENTS Interpreter Development NumPy)
  if(NOT Python3_FOUND)
    find_package(Python3 COMPONENTS Interpreter Development)
  endif(NOT Python3_FOUND)

  if(Python3_FOUND)
    message(VERBOSE "Found Python include dir: ${Python3_INCLUDE_DIRS}")
    set_property(SOURCE ${SwigFiles} PROPERTY COMPILE_OPTIONS "-builtin;-O")
    swig_add_library(PyBOSS TYPE SHARED LANGUAGE python SOURCES ${SwigFiles} ${ImplementationFiles})
    add_dependencies(${SWIG_MODULE_PyBOSS_REAL_NAME} arrow)
    set_property(TARGET ${SWIG_MODULE_PyBOSS_REAL_NAME} PROPERTY CXX_STANDARD 17)
    set_property(TARGET ${SWIG_MODULE_PyBOSS_REAL_NAME} PROPERTY SWIG_USE_TARGET_INCLUDE_DIRECTORIES TRUE)
    target_include_directories(${SWIG_MODULE_PyBOSS_REAL_NAME} SYSTEM PUBLIC ${Python3_INCLUDE_DIRS})
    target_include_directories(${SWIG_MODULE_PyBOSS_REAL_NAME} SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${SWIG_MODULE_PyBOSS_REAL_NAME} SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
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

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(Benchmarks Threads::Threads)

add_dependencies(Benchmarks googlebenchmark arrow)
if(ITT_NOTIFY_INCLUDE_DIR)
  message(VERBOSE "found itt notify header in ${ITT_NOTIFY_INCLUDE_DIR}")
  target_include_directories(Benchmarks SYSTEM PUBLIC ${ITT_NOTIFY_INCLUDE_DIR})
  target_link_libraries(Benchmarks ${ITT_NOTIFY_INCLUDE_DIR}/../lib64/libittnotify.a)
  target_link_libraries(Benchmarks dl)
  target_compile_options(Benchmarks PUBLIC -DWITH_ITT_NOTIFY)
endif(ITT_NOTIFY_INCLUDE_DIR)
target_link_libraries(Benchmarks ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark.a)
target_link_libraries(Benchmarks ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main.a)

add_dependencies(BOSS arrow)

# MLIR Libraries to link
set(mlir_libs
  ${interface_libs}
  ${dialect_libs}
  ${conversion_libs}
  MLIRIR
  MLIRParser
  MLIRPass
  MLIRSPIRV
  MLIRTranslation
  MLIRSupport
  MLIRTargetLLVMIR
  MLIRExecutionEngine
)

foreach (Target IN LISTS AllTargets)
  target_link_libraries(${Target} ${mlir_libs} Threads::Threads)
  target_link_libraries(${Target} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}arrow.a)
  target_link_libraries(${Target} ${BOSS_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}arrow_bundled_dependencies.a)
  set_property(TARGET ${Target} PROPERTY CXX_STANDARD 17)
  target_include_directories(${Target} SYSTEM PUBLIC ${BOSS_BINARY_DIR}/deps/include)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${Target} rt)
  endif()

  if(Mathematica_WSTP_FOUND)
    target_include_directories(${Target} PUBLIC ${Mathematica_INCLUDE_DIRS})
    target_link_libraries(${Target} ${Mathematica_WSTP_LIBRARY})
    if(UNIX AND NOT APPLE)
      target_link_libraries(${Target} uuid)
    endif()
    target_link_libraries(${Target} dl)
    target_compile_definitions(${Target} PUBLIC "${Mathematica_WSTP_DEFINITIONS}" "-DMATHEMATICA_KERNEL_EXECUTABLE=${Mathematica_KERNEL_EXECUTABLE}")
  endif(Mathematica_WSTP_FOUND)
endforeach()

mlir_check_link_libraries(BOSS)
mlir_check_link_libraries(Tests)
