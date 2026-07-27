include(FetchContent)

FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(catch2)

# Catch2 ships CMake helpers in extras/; catch_discover_tests registers every
# TEST_CASE as its own CTest entry, so `ctest` reports per-case results instead
# of one opaque pass/fail for the whole binary.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

file(GLOB_RECURSE CPEN_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

add_executable(cpen_tests ${CPEN_TEST_SOURCES})

# Shared test helpers are included as "support/...", relative to this directory,
# so a test file's location never affects the include path it has to spell.
target_include_directories(cpen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cpen_tests PRIVATE cpen cpen_warnings cpen_sanitizers Catch2::Catch2WithMain)

catch_discover_tests(cpen_tests)
