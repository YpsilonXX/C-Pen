add_subdirectory(glad)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)
