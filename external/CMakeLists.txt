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

# toml++, header-only, read by exactly one engine source.
#
# The include directory is marked SYSTEM so that cpen_warnings does not fire on
# half a megabyte of somebody else's code: unlike glad and stb_image, this one is
# compiled inside our own translation unit, where the shared warning set applies.
#
# TOML_EXCEPTIONS=0 turns a parse failure into a result object that has to be
# checked instead of a thrown exception. That is the engine's own convention —
# every other failure here travels through std::expected — and it means the one
# place a manifest is read has the same shape as the one place a PNG is decoded.
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus)
target_compile_definitions(tomlplusplus INTERFACE TOML_EXCEPTIONS=0)
