cmake_minimum_required(VERSION 3.16)
project(IRIS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

add_library(iris_core
    members/member1_cpu_memory/src/metrics_collector.cpp
    members/member2_disk_process/src/disk_process_monitor.cpp
    members/member2_disk_process/src/process_cpu_math.cpp
    members/member3_rules/src/deviation_calculator.cpp
    members/member3_rules/src/diagnostic_logger.cpp
    members/member3_rules/src/rule_engine.cpp
    members/member4_startup_dashboard/src/dashboard_server.cpp
    members/member4_startup_dashboard/src/startup_monitor.cpp
    shared/src/shared_metrics_store.cpp
)
target_include_directories(iris_core PUBLIC
    shared/include
    members/member1_cpu_memory/include
    members/member2_disk_process/include
    members/member3_rules/src
    members/member4_startup_dashboard/include
)
target_link_libraries(iris_core PUBLIC SQLite3::SQLite3)
if(WIN32)
    target_compile_definitions(iris_core PUBLIC _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
endif()
if(MSVC)
    target_compile_options(iris_core PRIVATE /W4 /permissive-)
    target_link_libraries(iris_core PUBLIC Pdh Psapi Advapi32 Ws2_32 Shell32)
else()
    target_compile_options(iris_core PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(iris_core PUBLIC pdh psapi advapi32 ws2_32 shell32)
endif()

configure_file(members/member4_startup_dashboard/ui/index.html index.html COPYONLY)

add_executable(iris_monitor app/main.cpp)
target_link_libraries(iris_monitor PRIVATE iris_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_process_cpu_math tests/test_process_cpu_math.cpp)
    target_link_libraries(test_process_cpu_math PRIVATE iris_core)
    add_test(NAME process_cpu_math COMMAND test_process_cpu_math)

    add_executable(test_shared_metrics_store tests/test_shared_metrics_store.cpp)
    target_link_libraries(test_shared_metrics_store PRIVATE iris_core)
    add_test(NAME shared_metrics_store COMMAND test_shared_metrics_store)

    add_executable(test_diagnostic_pipeline tests/test_diagnostic_pipeline.cpp)
    target_link_libraries(test_diagnostic_pipeline PRIVATE iris_core)
    add_test(NAME diagnostic_pipeline COMMAND test_diagnostic_pipeline)

    add_executable(iris_process_fixture_alpha tests/process_fixture.cpp)
    add_executable(iris_process_fixture_beta tests/process_fixture.cpp)
    add_executable(iris_process_fixture_short tests/process_fixture.cpp)
    add_executable(test_live_collector tests/test_live_collector.cpp)
    target_link_libraries(test_live_collector PRIVATE iris_core)
    add_test(NAME live_process_collector COMMAND test_live_collector
        $<TARGET_FILE:iris_process_fixture_alpha>
        $<TARGET_FILE:iris_process_fixture_beta>
        $<TARGET_FILE:iris_process_fixture_short>)
    set_tests_properties(live_process_collector PROPERTIES TIMEOUT 20)
endif()



