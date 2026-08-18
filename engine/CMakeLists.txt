include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw glm freetype)

file(GLOB_RECURSE CPEN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)

add_library(cpen STATIC ${CPEN_SOURCES})

target_include_directories(cpen
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(cpen PUBLIC cxx_std_23)

# GLFW must never pull in its own OpenGL headers — glad owns that role.
# PUBLIC so any consumer including <GLFW/glfw3.h> (e.g. games/demo) gets it too,
# making the glad-before-glfw include order requirement moot everywhere.
target_compile_definitions(cpen PUBLIC GLFW_INCLUDE_NONE)

target_link_libraries(cpen
    PRIVATE
        cpen_warnings
        cpen_sanitizers

        # PRIVATE, unlike every other dependency here: toml.hpp is included by
        # one source file and by no public header, so nothing that links the
        # engine has to see it — or has to be recompiled when it is bumped.
        tomlplusplus
    PUBLIC
        glfw
        glm::glm
        freetype
        glad
        stb_image
        miniaudio
)

# Where the engine's own files live in the source tree, and the function that
# puts them beside a program that needs them.
#
# Everything the engine loads is found relative to the executable, never relative
# to the working directory (see app/asset_roots.hh). That is what makes a built
# game runnable from anywhere and a shipped one runnable at all — but it means a
# program in the build tree has to have those files beside it too, which is what
# this does. Applied to the demo and to both test binaries; each gets its own
# copy, because each sits in a different directory.
set(CPEN_ENGINE_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets CACHE INTERNAL
    "Source directory of the assets the engine ships")

function(cpen_stage_engine_assets target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CPEN_ENGINE_ASSETS_DIR}
                $<TARGET_FILE_DIR:${target}>/engine/assets
        COMMENT "Staging engine assets beside ${target}"
        VERBATIM)
endfunction()
