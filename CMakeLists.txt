cmake_minimum_required(VERSION 3.25)

project(CPen LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPEN_BUILD_DEMO "Build the demo game executable" ON)
option(CPEN_BUILD_TESTS "Build the C-Pen test suite" ON)

# Shared warning set, linked PRIVATEly by first-party targets only.
# INTERFACE library rather than global CMAKE_CXX_FLAGS so that third-party
# code fetched into the build tree (glfw/freetype/glm/Catch2) stays silent.
add_library(cpen_warnings INTERFACE)

if(MSVC)
    target_compile_options(cpen_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(cpen_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wconversion
    )
endif()

add_subdirectory(external)
add_subdirectory(engine)

if(CPEN_BUILD_DEMO)
    add_subdirectory(games/demo)
endif()

if(CPEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
