add_subdirectory(glad)

# A compiled target rather than an INTERFACE one: stb_image's implementation is
# guarded by a macro and must be built exactly once. Doing that here instead of
# in an engine source keeps third-party code out of cpen_warnings, which
# -Wconversion alone would otherwise flood the build with.
add_library(stb_image STATIC stb/stb_image.c)
target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# PUBLIC so consumers see the same declarations the implementation was built
# with: reaching for stbi_load(const char*) then fails to compile rather than to
# link. See the comment in stb_image.c for why the stdio path is dropped.
target_compile_definitions(stb_image PUBLIC STBI_NO_STDIO)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)
