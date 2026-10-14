cmake_minimum_required(VERSION 3.16)
project(IRIS_Member4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

# The schema and diagnosis-query smoke tests are portable and can run on non-Windows hosts.
add_executable(iris_member4_smoke_test
    src/database.cpp
    src/dashboard.cpp
    tests/member4_smoke_test.cpp
)
target_include_directories(iris_member4_smoke_test PRIVATE src)
target_link_libraries(iris_member4_smoke_test PRIVATE SQLite::SQLite3)
enable_testing()
add_test(NAME member4_smoke_test COMMAND iris_member4_smoke_test)

# Registry enumeration and system hardware defaults require the Windows SDK.
if(WIN32)
    add_executable(iris_member4
        src/main.cpp
        src/database.cpp
        src/system_setup.cpp
        src/dashboard.cpp
    )
    target_include_directories(iris_member4 PRIVATE src)
    target_link_libraries(iris_member4 PRIVATE SQLite::SQLite3 Advapi32)
    if(MSVC)
        target_compile_options(iris_member4 PRIVATE /W4 /permissive-)
    endif()
else()
    message(STATUS "Windows executable skipped: system_setup.cpp uses Win32 APIs. Portable smoke test is enabled.")
endif()
