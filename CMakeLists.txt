cmake_minimum_required(VERSION 3.20)

project(Parser VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 20)

add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-deprecated-declarations # Allow Deprecated Things
        -g
)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(
            -march=native
            -O3
            -ffast-math
            -flto
            -ffinite-math-only
            -fassociative-math
            -funroll-loops
            -ftree-vectorize
    )
endif ()

# **Build standalone parser executable**
add_executable(parser src/parser.cpp)
