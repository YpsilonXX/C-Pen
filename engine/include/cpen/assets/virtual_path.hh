#ifndef CPEN_ASSETS_VIRTUAL_PATH_HH
#define CPEN_ASSETS_VIRTUAL_PATH_HH

#include "cpen/core/error.hh"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cpen::assets
{
    /// Everything about the *name* of an asset, before any file is opened.
    ///
    /// A virtual path is what the engine, the manifest and eventually the script
    /// language spell: UTF-8, '/' as the only separator, always relative to a
    /// mounted root — "fonts/ui.ttf", "sprites/alice/happy.png". It is not a
    /// std::filesystem::path and deliberately does not behave like one: it has no
    /// drive, no parent directory to climb into, and one spelling per file.
    ///
    /// The rules below exist because the same content has to load on Windows and
    /// on Linux from the same repository. Every one of them encodes a way that
    /// stops being true when the machine changes.

    /// Checks that `path` is a virtual path at all.
    ///
    /// Rejects, with a message naming what is wrong and why it matters:
    ///
    ///   - an empty path, or an empty segment ("bg//room.png", "bg/") — almost
    ///     always a join that concatenated one separator too many;
    ///   - a leading '/' or a drive letter — an absolute path escapes the roots
    ///     entirely and means a different file on every machine;
    ///   - a backslash — it works as a separator on Windows and is an ordinary
    ///     filename character on Linux, so a path written with one loads for the
    ///     author and not for anybody else;
    ///   - "." and ".." — there is no parent directory to climb to in an archive,
    ///     and a path built from player-supplied text could otherwise reach out of
    ///     the game directory entirely;
    ///   - control characters and ':' — unrepresentable or reserved on Windows
    ///     file systems;
    ///   - a segment ending in a space or a dot — Windows silently strips both
    ///     when creating a file, so the name asked for is not the name that
    ///     appears;
    ///   - the reserved device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9), with or
    ///     without an extension — Windows refuses to create such a file at all,
    ///     so an asset named this way exists only on Linux.
    ///
    /// Case is *not* checked here: a virtual path may be spelled in any case, and
    /// whether the file system agrees is a question about the disk, answered by
    /// audit_path_case.
    std::expected<void, core::Error> validate_virtual_path(std::string_view path);

    /// Splits a virtual path on '/'. No validation: a caller that wants the rules
    /// enforced calls validate_virtual_path first.
    std::vector<std::string_view> split_virtual_path(std::string_view path);

    /// One segment of a path whose spelling on disk differs from the spelling
    /// that was asked for.
    struct PathCaseMismatch
    {
        /// The segment as written by whoever asked for the asset.
        std::string requested;

        /// The name the file or directory actually has.
        std::string actual;
    };

    /// Compares each segment of `virtual_path` with the names that really exist
    /// under `root`, and reports those that differ only in case.
    ///
    /// This is the check for the single most expensive portability mistake a
    /// game can make. Windows and macOS match file names without regard to case,
    /// so "sprites/Alice.png" opens a file named "alice.png" and the author never
    /// learns that they disagree. Linux matches exactly: there the same asset is
    /// simply missing. The failure surfaces on somebody else's machine, long
    /// after the file was named, and it is invisible in a diff.
    ///
    /// Scanning is the only way to ask: on a case-insensitive file system,
    /// exists() answers yes for the wrong spelling too, so the real name has to
    /// be read out of the directory and compared. That costs one directory scan
    /// per segment, which is why the audit is a development-time setting rather
    /// than something every load pays for.
    ///
    /// An empty result means agreement — or that the file is not there at all,
    /// which is a different failure and not this function's to report. Only ASCII
    /// letters are folded: matching "Алиса" against "алиса" would need full
    /// Unicode case folding, which is a table this engine does not carry. Names
    /// outside ASCII are compared exactly, so a mismatch in them is reported as a
    /// missing file rather than as a case problem.
    std::vector<PathCaseMismatch> audit_path_case(const std::filesystem::path& root,
                                                  std::string_view virtual_path);

    /// What happened to the asset whose name did not match, which decides what
    /// the report has to tell the reader.
    ///
    /// Both outcomes are the same mistake seen from the two kinds of file system,
    /// and each is only visible on one of them. On Windows or macOS the file
    /// opens regardless of case, so the game runs and the author learns nothing —
    /// until it reaches a case-sensitive machine, where the same asset is missing.
    /// On Linux it is missing right now, and the useful thing to say is not "not
    /// found" but "found, spelled differently".
    enum class CaseMismatchOutcome : std::uint8_t
    {
        /// The file was loaded, because this file system ignores case.
        LOADED_ANYWAY,

        /// The file was not loaded; only a differently-cased name exists.
        NOT_FOUND,
    };

    /// Renders the mismatch report that goes to the log.
    ///
    /// A plain string rather than a log call, so a test can assert on what the
    /// author will read, and so the same text can be repeated in the summary
    /// printed when the game exits.
    ///
    /// The framing is loud on purpose. This diagnostic reports something that
    /// works perfectly on the machine reading it, which is exactly the kind of
    /// warning that gets scrolled past; it has to survive being one line among
    /// hundreds in a console. `consequence`, when given, is appended by the layer
    /// that knows what the asset was for ("the background will be missing"), and
    /// the generic consequence is stated regardless.
    std::string format_case_mismatch_report(std::string_view virtual_path,
                                            const std::vector<PathCaseMismatch>& mismatches,
                                            CaseMismatchOutcome outcome,
                                            std::string_view consequence = {});
}

#endif //CPEN_ASSETS_VIRTUAL_PATH_HH
