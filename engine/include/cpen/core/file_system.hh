#ifndef CPEN_CORE_FILE_SYSTEM_HH
#define CPEN_CORE_FILE_SYSTEM_HH

#include "cpen/core/error.hh"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cpen::core
{
    /// Reads a whole file into memory.
    ///
    /// Whole-file rather than streaming because everything the engine reads is
    /// consumed in one go — an image is decoded from a complete buffer, a font
    /// face is handed to FreeType entire, a script is compiled from its full
    /// text. Audio is the one exception, and miniaudio does its own streaming
    /// through its own callbacks rather than through this.
    ///
    /// Reports FILE_NOT_FOUND when the path names nothing, and UNSPECIFIED for
    /// everything else — a directory given where a file was meant, a permission
    /// refusal, a read cut short. That distinction exists because a missing asset
    /// is the one failure a caller acts on differently: it can fall back to a
    /// placeholder, whereas an unreadable file is a fault to report.
    std::expected<std::vector<std::byte>, Error> read_file_bytes(
        const std::filesystem::path& path);

    /// Reads a whole file as text, normalised by normalize_text.
    ///
    /// Separate from read_file_bytes rather than a flag on it, so that the two
    /// cannot be confused at a call site: an image read through this would be
    /// quietly corrupted by the line-ending pass.
    std::expected<std::string, Error> read_file_text(const std::filesystem::path& path);

    /// Puts text read from a file into the form the rest of the engine assumes:
    /// no byte-order mark, LF line endings.
    ///
    /// A UTF-8 BOM is stripped because it is not text — it is a marker some
    /// editors write, and left in place it becomes part of the first token a
    /// lexer sees, or of the first key in a manifest. CRLF is folded to LF so
    /// that a script authored on Windows lexes identically to the same file
    /// authored on Linux, and so that a line's length is not one longer than it
    /// looks.
    ///
    /// A lone CR is deliberately left alone. Nothing produces it any more except
    /// pre-OS X Macs, and inside a string literal it is data the author wrote;
    /// rewriting it would be a guess made silently.
    ///
    /// Takes the string by value and edits it in place: the caller almost always
    /// owns a freshly read buffer with nobody else looking at it.
    std::string normalize_text(std::string text);

    /// Builds a path from UTF-8 bytes.
    ///
    /// The engine holds every string as UTF-8 (script sources, manifest entries,
    /// asset identifiers), and this is the one point where that meets the
    /// operating system. It matters only on Windows, where a path is natively
    /// UTF-16 and the plain std::string constructor interprets its bytes in the
    /// active code page — which turns any non-ASCII name into mojibake, or into a
    /// file that cannot be opened at all. On Linux the bytes pass through
    /// unchanged.
    std::filesystem::path path_from_utf8(std::string_view text);

    /// The inverse of path_from_utf8: a path as UTF-8 bytes, fit for a log line,
    /// an error message or a manifest key.
    std::string path_to_utf8(const std::filesystem::path& path);
}

#endif //CPEN_CORE_FILE_SYSTEM_HH
