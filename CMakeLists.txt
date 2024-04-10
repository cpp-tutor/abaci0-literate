cmake_minimum_required(VERSION 3.25.1)
project(abaci0-literate)

# Compiler settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/utility)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/parser)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/codegen)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/engine)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Find fmt
find_package(fmt REQUIRED)

# Source files
set(SOURCES
    src/lib/Abaci.cpp
    src/utility/Utility.cpp
    src/utility/Environment.cpp
    src/parser/Parse.cpp
    src/codegen/ExprCodeGen.cpp
    src/codegen/StmtCodeGen.cpp
    src/codegen/TypeCodeGen.cpp
    src/engine/Cache.cpp
    src/engine/JIT.cpp
    src/main.cpp
)

# Executable
add_executable(abaci0 ${SOURCES})

# Link LLVM libraries
llvm_map_components_to_libnames(LLVM_LIBS core executionengine interpreter analysis native bitwriter orcjit)
target_link_libraries(abaci0 ${LLVM_LIBS} fmt::fmt)
