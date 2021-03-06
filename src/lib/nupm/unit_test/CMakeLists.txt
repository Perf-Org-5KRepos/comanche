cmake_minimum_required (VERSION 3.5.1 FATAL_ERROR)

project(libnupm-test CXX)

enable_language(CXX C)

set(GCC_COVERAGE_COMPILE_FLAGS "-std=c++14 -g -O2 -fPIC")

find_library(GTEST_LIB NAMES GTEST PATHS /usr/local/lib,/usr/lib)

include_directories(../src)
include_directories(${CMAKE_INSTALL_PREFIX}/include)
link_directories(${CMAKE_INSTALL_PREFIX}/lib)
link_directories(${CMAKE_INSTALL_PREFIX}/lib64)
link_directories(/usr/local/lib64)

add_definitions(${GCC_COVERAGE_COMPILE_FLAGS} -DCONFIG_DEBUG)

add_executable(libnupm-test test1.cpp)
target_link_libraries(libnupm-test ${ASAN_LIB} common numa ndctl gtest pthread dl nupm pmemobj pmempool z memkind)  # add profier for google profiler

add_executable(libnupm-test2 test2.cpp)
target_compile_options(libnupm-test2 PUBLIC "$<$<CONFIG:Debug>:-O0>")
target_compile_options(libnupm-test2 PUBLIC -g -pedantic -Wall -Werror -Wextra -Wcast-align -Wcast-qual -Wconversion -Weffc++ -Wold-style-cast -Wredundant-decls -Wshadow -Wtype-limits -Wunused-parameter -Wwrite-strings)
target_link_libraries(libnupm-test2 ${ASAN_LIB} gtest pthread dl nupm)  # add profier for google profiler
