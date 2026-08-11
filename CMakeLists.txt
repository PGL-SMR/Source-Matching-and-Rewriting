cmake_minimum_required(VERSION 3.10)

# Define the Project Name and Description
project(smr
    VERSION 1.0.0
    DESCRIPTION "Source-based Matching and Rewriting tool."
    LANGUAGES CXX C)
configure_file(smr/Version.h.in smr/Version.h)

# Ensure C++17 is used.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Disable optimizations and use the newer dwarf version for debugging.
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Building in Debug mode")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -gdwarf-4 -O0")
else()
    message(STATUS "Building in Release mode")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O3")
endif()

# Add cmake tools directory.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Search for MLIR/LLVM CMake modules.
find_package(MLIR REQUIRED CONFIG)
message(STATUS "Found MLIR: ${MLIR_DIR} (version: ${LLVM_PACKAGE_VERSION})")

find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang: ${CLANG_CMAKE_DIR} (version: ${LLVM_PACKAGE_VERSION})")

# Find Boost on your system
find_package(Boost REQUIRED COMPONENTS serialization)

# Include Boost in the project.
list(APPEND CMAKE_MODULE_PATH "${Boost_CMAKE_DIR}")
include_directories(${Boost_INCLUDE_DIRS})

# Include LLVM in the project.
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_BUILD_LIBRARY_DIR})
add_definitions(${LLVM_DEFINITIONS})
set(LLVM_LIBS
    LLVMSupport)

# Include MLIR in the project.
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
include(AddMLIR)
include_directories(${MLIR_INCLUDE_DIRS})
add_definitions(${MLIR_DEFINITIONS})
set(MLIR_LIBS_DIR "${MLIR_MAIN_SRC_DIR}/build/lib" CACHE STRING "MLIR libraries path")
set(MLIR_LIBS
    MLIRIR
    MLIRParser
    MLIRFuncDialect
    MLIRArithDialect)

list(APPEND CMAKE_MODULE_PATH "${CLANG_CMAKE_DIR}")
include(AddClang)
include_directories(${CLANG_INCLUDE_DIRS})
add_definitions(${CLANG_DEFINITIONS})
set(CLANG_LIBS
    MLIRCIR
    clangCIR)

# LLVM module compiled without RTTI: abort.
IF (NOT LLVM_ENABLE_RTTI)
    message(FATAL_ERROR "
    The LLVM binaries being used were compiled without RTTI.
    SMR requires an LLVM binary with RTTI functionalities.
    ")
ENDIF ()

# Include clang format targets.
include(ClangFormat)
include(ClangTidy)
include(CppCheck)

# Add SMR source files.
add_subdirectory(smr)

