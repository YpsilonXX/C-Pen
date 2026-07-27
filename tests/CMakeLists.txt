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

file(GLOB_RECURSE CPEN_ALL_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

# The suite is split by what a test needs to run, not by what it covers.
#
# Everything under gpu/ requires a GL context and therefore a driver, which makes
# it unrunnable in exactly the environments where an automated suite is most
# useful. Keeping it in a second executable lets cpen_tests keep the promise that
# it runs anywhere, and lets the GPU tests be as heavy as they need to be —
# rendering into a framebuffer and reading pixels back is the only honest way to
# test a sprite batch, and it has no business happening on every `ctest` run.
#
# support/ compiles into both: the trace listener and the log capture guard are
# useful either side of the divide.
set(CPEN_TEST_SOURCES ${CPEN_ALL_TEST_SOURCES})
list(FILTER CPEN_TEST_SOURCES EXCLUDE REGEX "/gpu/")

set(CPEN_GPU_TEST_SOURCES ${CPEN_ALL_TEST_SOURCES})
list(FILTER CPEN_GPU_TEST_SOURCES INCLUDE REGEX "/(gpu|support)/")

add_executable(cpen_tests ${CPEN_TEST_SOURCES})

# Shared test helpers are included as "support/...", relative to this directory,
# so a test file's location never affects the include path it has to spell.
target_include_directories(cpen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cpen_tests PRIVATE cpen cpen_warnings cpen_sanitizers Catch2::Catch2WithMain)

catch_discover_tests(cpen_tests)

if(CPEN_BUILD_GPU_TESTS)
    add_executable(cpen_gpu_tests ${CPEN_GPU_TEST_SOURCES})

    target_include_directories(cpen_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Sanitizers are carried here as everywhere else, with one caveat worth
    # knowing before reading a report: a graphics driver allocates freely at
    # context creation and frees none of it, so a LeakSanitizer run on this target
    # says far more about the driver than about the engine. Use
    # LSAN_OPTIONS=detect_leaks=0, or a suppression file, when running it.
    target_link_libraries(cpen_gpu_tests
        PRIVATE cpen cpen_warnings cpen_sanitizers Catch2::Catch2WithMain)

    # Deliberately not catch_discover_tests: these are run by hand, never as part
    # of the default sweep. See the option's comment in the top-level file.
endif()
