cmake_minimum_required(VERSION 3.25)

project(CPen LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPEN_BUILD_DEMO "Build the demo game executable" ON)
option(CPEN_BUILD_TESTS "Build the C-Pen test suite" ON)

# Tests that need a real GL context live in their own executable, which CTest is
# deliberately not told about: `ctest` must stay runnable anywhere, including on a
# machine with no GPU or display, while these are run by hand. Turn this off to
# build the suite without them at all.
option(CPEN_BUILD_GPU_TESTS "Build tests that require an OpenGL context" ON)

# Comma-separated sanitizer list, empty to disable. Configure a second build
# directory with it rather than switching the main one back and forth:
#   cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCPEN_SANITIZE=address,undefined
set(CPEN_SANITIZE "" CACHE STRING
    "Sanitizers to build first-party targets with (address,undefined / thread / empty)")
set_property(CACHE CPEN_SANITIZE PROPERTY STRINGS
    "" "address" "undefined" "address,undefined" "thread" "thread,undefined")

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

# Sanitizer flags, carried the same way as the warning set: an INTERFACE target
# linked by first-party code only. Third-party sources fetched into the build
# tree stay uninstrumented, which is supported — a partially instrumented program
# simply reports nothing about the code that was left out.
#
# The flags must reach both compilation and linking: the instrumentation is
# emitted by the compiler, but the runtime that reports on it is pulled in by the
# linker. That is also why every first-party target links this, executables
# included, and not just the engine library.
add_library(cpen_sanitizers INTERFACE)

if(CPEN_SANITIZE)
    if(CPEN_SANITIZE MATCHES "address" AND CPEN_SANITIZE MATCHES "thread")
        message(FATAL_ERROR
            "CPEN_SANITIZE: address and thread sanitizers cannot be combined; use separate build directories")
    endif()

    if(MSVC)
        if(NOT CPEN_SANITIZE STREQUAL "address")
            message(FATAL_ERROR "CPEN_SANITIZE: MSVC supports 'address' only, got '${CPEN_SANITIZE}'")
        endif()
        target_compile_options(cpen_sanitizers INTERFACE /fsanitize=address)
    else()
        # -fno-omit-frame-pointer keeps the reported stacks readable; without it
        # optimised frames collapse and the origin of an error is guesswork.
        # -fno-sanitize-recover turns an undefined-behaviour report into a failure
        # rather than a printed warning, so a test run cannot pass through one.
        target_compile_options(cpen_sanitizers INTERFACE
            -fsanitize=${CPEN_SANITIZE}
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
        )
        target_link_options(cpen_sanitizers INTERFACE -fsanitize=${CPEN_SANITIZE})
    endif()

    message(STATUS "C-Pen: sanitizers enabled (${CPEN_SANITIZE})")

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        message(WARNING
            "CPEN_SANITIZE is set without CMAKE_BUILD_TYPE; reports will have no line numbers. Configure with -DCMAKE_BUILD_TYPE=Debug or RelWithDebInfo.")
    endif()
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
