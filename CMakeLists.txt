cmake_minimum_required(VERSION 3.20)
project(quant_finance_cpp VERSION 1.0.0 LANGUAGES CXX)

# C++ port of Quant-Finance-Library (Java). Zero external dependencies:
# the standard library only, tests on a vendored micro-harness.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Library sources are globbed so port agents only ADD files and never
# edit this build script (CONFIGURE_DEPENDS re-globs on build).
file(GLOB_RECURSE QFL_SOURCES CONFIGURE_DEPENDS src/quantfinlib/*.cpp)
add_library(quantfinlib STATIC ${QFL_SOURCES})
target_include_directories(quantfinlib PUBLIC src)

# FIX engine (Phase 4B) needs winsock on Windows; tests link only against
# quantfinlib, so the socket lib is attached here rather than per-test.
if(WIN32)
    target_link_libraries(quantfinlib PUBLIC ws2_32)
endif()

# One test executable per tests/*_test.cpp, registered with CTest.
enable_testing()
file(GLOB QFL_TESTS CONFIGURE_DEPENDS tests/*_test.cpp)
foreach(test_src ${QFL_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE quantfinlib)
    target_include_directories(${test_name} PRIVATE tests)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# One benchmark executable per benchmarks/*_bench.cpp (built, never run by
# CTest: nanosecond numbers belong to a quiet, manually invoked machine).
file(GLOB QFL_BENCHES CONFIGURE_DEPENDS benchmarks/*_bench.cpp)
foreach(bench_src ${QFL_BENCHES})
    get_filename_component(bench_name ${bench_src} NAME_WE)
    add_executable(${bench_name} ${bench_src})
    target_link_libraries(${bench_name} PRIVATE quantfinlib)
    target_compile_options(${bench_name} PRIVATE -O3)
endforeach()
