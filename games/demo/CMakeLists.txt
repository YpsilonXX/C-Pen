file(GLOB_RECURSE CPEN_DEMO_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)

add_executable(cpen_demo ${CPEN_DEMO_SOURCES})

target_link_libraries(cpen_demo PRIVATE cpen cpen_warnings cpen_sanitizers)

# The demo's own files, staged the same way and for the same reason: the engine
# looks for them beside the executable, so in the build tree they have to be
# there. A shipped game is this same layout, produced by whatever packages it.
add_custom_command(TARGET cpen_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:cpen_demo>/game/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/script $<TARGET_FILE_DIR:cpen_demo>/game/script
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/game.toml $<TARGET_FILE_DIR:cpen_demo>/game/game.toml
    COMMENT "Staging the demo's game directory"
    VERBATIM)

cpen_stage_engine_assets(cpen_demo)
