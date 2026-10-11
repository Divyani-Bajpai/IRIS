cmake_minimum_required(VERSION 3.16)
project(iris_cpu_memory C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SQLite amalgamation, compiled as C. Warnings off because it is not our code.
add_library(sqlite3_lib STATIC ../third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ../third_party/sqlite)
if(NOT MSVC)
    target_compile_options(sqlite3_lib PRIVATE -w)
endif()

# Library that other IRIS modules can link against
add_library(iris_cpu_memory STATIC
    src/metrics_collector.cpp
    src/cpu_memory_db.cpp
    src/baseline.cpp)
target_include_directories(iris_cpu_memory PUBLIC include)
target_link_libraries(iris_cpu_memory PUBLIC pdh sqlite3_lib)

if(MSVC)
    target_compile_options(iris_cpu_memory PRIVATE /W3)
else()
    target_compile_options(iris_cpu_memory PRIVATE -Wall -Wextra)
endif()

# Test programs
add_executable(cpu_memory_demo demo/cpu_memory_demo.cpp)
target_link_libraries(cpu_memory_demo PRIVATE iris_cpu_memory)

add_executable(baseline_demo demo/baseline_demo.cpp)
target_link_libraries(baseline_demo PRIVATE iris_cpu_memory)