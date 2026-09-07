cmake_minimum_required(VERSION 3.20)
project(fpga-gpu-compiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM Package
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(include)
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Map required LLVM components
llvm_map_components_to_libnames(LLVM_LIBS
    Core
    IRReader
    Passes
    Support
    TransformUtils
    ScalarOpts
    InstCombine
    Analysis
)

# Compiler Binary
add_executable(fpgagpu-llc
    src/main.cpp
    src/FpgaGpuBackend.cpp
    src/FpgaGpuISel.cpp
    src/FpgaGpuRegAlloc.cpp
    src/FpgaGpuEmitter.cpp
    src/FpgaGpuAssembler.cpp
)

target_link_libraries(fpgagpu-llc PRIVATE ${LLVM_LIBS})

# Install target to bin/
install(TARGETS fpgagpu-llc DESTINATION bin)
