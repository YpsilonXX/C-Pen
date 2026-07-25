include(FetchContent)

FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(catch2)

file(GLOB_RECURSE CPEN_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

add_executable(cpen_tests ${CPEN_TEST_SOURCES})

target_link_libraries(cpen_tests PRIVATE cpen Catch2::Catch2WithMain)

add_test(NAME cpen_tests COMMAND cpen_tests)
