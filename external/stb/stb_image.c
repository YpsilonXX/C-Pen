/* The single translation unit that compiles stb_image's implementation.
 *
 * stb_image is distributed as one header holding both the interface and the
 * implementation, the latter guarded by STB_IMAGE_IMPLEMENTATION. Defining that
 * macro in exactly one file, and building it as a library of its own, keeps the
 * implementation out of every consumer's translation unit and out of the
 * engine's warning set — the same arrangement glad already uses.
 *
 * STBI_NO_STDIO, set on the target so that consumers compile against the same
 * declarations, removes the stbi_load(const char*) family. The engine reads
 * files itself and decodes from memory, so that the file-opening path is the
 * standard library's rather than stb's: a std::filesystem::path is widened
 * correctly on Windows, while stb's fopen(const char*) mangles any path that is
 * not representable in the active code page.
 */

#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"
