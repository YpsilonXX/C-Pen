#include "cpen/assets/virtual_path.hh"

#include "cpen/core/file_system.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <system_error>

namespace cpen::assets
{
    namespace
    {
        constexpr char ascii_lower(const char character) noexcept
        {
            return character >= 'A' && character <= 'Z'
                       ? static_cast<char>(character - 'A' + 'a')
                       : character;
        }

        bool equal_ignoring_ascii_case(const std::string_view left,
                                       const std::string_view right) noexcept
        {
            return std::ranges::equal(left, right, [](const char first, const char second)
            {
                return ascii_lower(first) == ascii_lower(second);
            });
        }

        /// Names Windows reserves for devices. A file called any of these, with or
        /// without an extension, cannot be created there at all — so an asset named
        /// "nul.png" on Linux is a game that does not ship.
        constexpr std::array<std::string_view, 22> RESERVED_DEVICE_NAMES = {
            "con", "prn", "aux", "nul",
            "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
            "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
        };

        bool is_reserved_device_name(const std::string_view segment) noexcept
        {
            const std::string_view stem = segment.substr(0, segment.find('.'));

            return std::ranges::any_of(RESERVED_DEVICE_NAMES,
                                       [stem](const std::string_view reserved)
            {
                return equal_ignoring_ascii_case(stem, reserved);
            });
        }

        std::unexpected<core::Error> reject(const std::string_view path,
                                            const std::string_view reason)
        {
            return std::unexpected(core::make_error(core::ErrorCode::INVALID_FORMAT,
                                                    "asset path '{}': {}", path, reason));
        }
    }

    std::expected<void, core::Error> validate_virtual_path(const std::string_view path)
    {
        if (path.empty())
        {
            return reject(path, "an asset path cannot be empty");
        }

        if (path.front() == '/')
        {
            return reject(path,
                          "starts at the file system root; asset paths are relative to a "
                          "mounted root, so that the same game runs from any directory");
        }

        for (const char character : path)
        {
            if (static_cast<unsigned char>(character) < 0x20)
            {
                return reject(path, "contains a control character");
            }

            if (character == '\\')
            {
                return reject(path,
                              "uses a backslash; '/' is the only separator, because a "
                              "backslash separates directories on Windows and is an "
                              "ordinary character in a file name on Linux");
            }

            if (character == ':')
            {
                return reject(path,
                              "contains ':', which names a drive or an alternate data "
                              "stream on Windows and cannot appear in a file name there");
            }
        }

        for (const std::string_view segment : split_virtual_path(path))
        {
            if (segment.empty())
            {
                return reject(path,
                              "has an empty component; a path ends in a file name and "
                              "never carries two separators in a row");
            }

            if (segment == "." || segment == "..")
            {
                return reject(path,
                              "navigates with '.' or '..'; an asset path names one file "
                              "in one place, and an archive has no directory to climb to");
            }

            if (segment.back() == ' ' || segment.back() == '.')
            {
                return reject(path,
                              "has a component ending in a space or a dot, which Windows "
                              "strips when creating the file — the name asked for would "
                              "not be the name on disk");
            }

            if (is_reserved_device_name(segment))
            {
                return reject(path,
                              "uses a name Windows reserves for a device (CON, PRN, AUX, "
                              "NUL, COM1-9, LPT1-9); a file with that name cannot exist "
                              "there");
            }
        }

        return {};
    }

    std::vector<std::string_view> split_virtual_path(const std::string_view path)
    {
        std::vector<std::string_view> segments;
        std::size_t start = 0;

        while (true)
        {
            const std::size_t separator = path.find('/', start);

            if (separator == std::string_view::npos)
            {
                segments.push_back(path.substr(start));
                return segments;
            }

            segments.push_back(path.substr(start, separator - start));
            start = separator + 1;
        }
    }

    std::vector<PathCaseMismatch> audit_path_case(const std::filesystem::path& root,
                                                  const std::string_view virtual_path)
    {
        std::vector<PathCaseMismatch> mismatches;
        std::filesystem::path current = root;

        for (const std::string_view segment : split_virtual_path(virtual_path))
        {
            if (segment.empty())
            {
                break;
            }

            std::error_code error;
            std::filesystem::directory_iterator entry(current, error);
            const std::filesystem::directory_iterator end;

            bool found_exact = false;
            std::string actual_name;

            // The error_code form of increment() as well as of the constructor: a
            // directory that vanishes mid-scan is not a case problem, and this
            // runs inside a diagnostic that must never become the reason a game
            // stops.
            for (; !error && entry != end; entry.increment(error))
            {
                const std::string name = core::path_to_utf8(entry->path().filename());

                if (name == segment)
                {
                    found_exact = true;
                    break;
                }

                if (equal_ignoring_ascii_case(name, segment))
                {
                    actual_name = name;
                }
            }

            if (found_exact)
            {
                current /= core::path_from_utf8(segment);
                continue;
            }

            if (actual_name.empty())
            {
                // Nothing here by that name in any casing: the file is missing,
                // which is somebody else's diagnostic.
                break;
            }

            mismatches.push_back(PathCaseMismatch{
                .requested = std::string{segment},
                .actual = actual_name,
            });

            current /= core::path_from_utf8(actual_name);
        }

        return mismatches;
    }

    std::string format_case_mismatch_report(const std::string_view virtual_path,
                                            const std::vector<PathCaseMismatch>& mismatches,
                                            const CaseMismatchOutcome outcome,
                                            const std::string_view consequence)
    {
        constexpr std::string_view RULE =
            "!!! ==================================================================== !!!";

        // The path as it would have to be written to match what is on disk. The
        // mismatches are recorded in path order, so they are consumed in order.
        std::string corrected;
        std::size_t next_mismatch = 0;

        for (const std::string_view segment : split_virtual_path(virtual_path))
        {
            if (!corrected.empty())
            {
                corrected += '/';
            }

            if (next_mismatch < mismatches.size() &&
                mismatches[next_mismatch].requested == segment)
            {
                corrected += mismatches[next_mismatch].actual;
                ++next_mismatch;
            }
            else
            {
                corrected += segment;
            }
        }

        const std::string_view headline =
            outcome == CaseMismatchOutcome::LOADED_ANYWAY
                ? "ASSET NAME CASE MISMATCH - loads here, WILL NOT LOAD on Linux"
                : "ASSET NOT LOADED - the file exists, spelled in a different case";

        std::string report = std::format(
            "\n{0}\n"
            "!!!  {1}\n"
            "{0}\n"
            "  asked for : {2}\n"
            "  on disk   : {3}\n",
            RULE, headline, virtual_path, corrected);

        for (const PathCaseMismatch& mismatch : mismatches)
        {
            report += std::format("  differs   : '{}' is spelled '{}'\n",
                                  mismatch.requested, mismatch.actual);
        }

        if (outcome == CaseMismatchOutcome::LOADED_ANYWAY)
        {
            report +=
                "  This file system matches names without regard to case, so the asset\n"
                "  loaded and nothing looks wrong. Linux matches exactly: there the file\n"
                "  is not found at all and the asset is missing.\n";
        }
        else
        {
            report +=
                "  This file system matches names exactly, so the asset did not load.\n"
                "  It would have loaded on Windows and on macOS, which is why the name\n"
                "  was able to end up like this in the first place.\n";
        }

        if (!consequence.empty())
        {
            report += std::format("  Consequence: {}\n", consequence);
        }

        report +=
            "  Fix one side so both spell it the same way - rename the file, or\n"
            "  correct the identifier that asks for it. Nothing is renamed for you:\n"
            "  the engine cannot see what else refers to this file.\n";

        report += RULE;
        report += '\n';

        return report;
    }
}
