cmake_minimum_required(VERSION 3.25)

project(CPen LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPEN_BUILD_DEMO "Build the demo game executable" ON)
option(CPEN_BUILD_TESTS "Build the C-Pen test suite" ON)

add_subdirectory(external)
add_subdirectory(engine)

if(CPEN_BUILD_DEMO)
    add_subdirectory(games/demo)
endif()

if(CPEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
