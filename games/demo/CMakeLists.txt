file(GLOB_RECURSE CPEN_DEMO_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)

add_executable(cpen_demo ${CPEN_DEMO_SOURCES})

target_link_libraries(cpen_demo PRIVATE cpen cpen_warnings)
