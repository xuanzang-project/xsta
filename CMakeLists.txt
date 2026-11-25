cmake_minimum_required(VERSION 3.16)
project(prormrxcn_sta VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sta
    src/liberty.cpp
    src/json.cpp
    src/netlist.cpp
    src/timing.cpp
    src/report.cpp
)
target_include_directories(sta PUBLIC include)
target_compile_options(sta PRIVATE -Wall -Wextra -Wpedantic)

add_executable(xsta src/main.cpp)
target_link_libraries(xsta PRIVATE sta)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        add_executable(sta_tests tests/sta_tests.cpp)
        target_link_libraries(sta_tests PRIVATE sta GTest::gtest_main)
        include(GoogleTest)
        gtest_discover_tests(sta_tests)
    else()
        message(STATUS "GoogleTest not found; build the standalone smoke tests with make test")
    endif()
endif()

install(TARGETS xsta RUNTIME DESTINATION bin)
