#ifndef CPEN_TESTS_SUPPORT_SYSTEM_FONT_HH
#define CPEN_TESTS_SUPPORT_SYSTEM_FONT_HH

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace cpen::test
{
    /// Finds a typeface installed on the machine running the tests.
    ///
    /// The engine ships no font of its own yet — it will have to, since a game
    /// cannot assume the player has one, but that belongs with the asset layer
    /// rather than in a test fixture. Until then the cases that need a real
    /// typeface borrow one, and skip themselves when the machine has none.
    ///
    /// The cost of skipping is small and worth naming: everything that can be
    /// tested without a typeface already is — the UTF-8 decoder and the line
    /// breaker both take their input as plain values — and what remains needs a GL
    /// context anyway, so it lives in the target that already refuses to run
    /// without a driver.
    ///
    /// Candidates are matched by file name rather than by full path, because every
    /// distribution puts them somewhere different. Each is a face that carries
    /// Cyrillic as well as Latin, which the cases rely on: a fallback picked for
    /// merely existing could pass every Latin assertion and fail the one that
    /// matters most for this engine.
    inline std::optional<std::filesystem::path> find_system_font()
    {
        static const std::optional<std::filesystem::path> located = []
            -> std::optional<std::filesystem::path>
        {
            constexpr std::array<std::string_view, 8> CANDIDATES = {
                "DejaVuSans.ttf",  "LiberationSans-Regular.ttf",
                "NotoSans-Regular.ttf", "FreeSans.otf",
                "Ubuntu-R.ttf",    "arial.ttf",
                "Arial.ttf",       "segoeui.ttf",
            };

            constexpr std::array<std::string_view, 6> ROOTS = {
                "/usr/share/fonts",       "/usr/local/share/fonts",
                "/System/Library/Fonts",  "/Library/Fonts",
                "C:/Windows/Fonts",       "C:/Windows/fonts",
            };

            // Collected rather than returned on the first hit, so that the order of
            // CANDIDATES decides which face is used and not the order the
            // filesystem happens to walk in. A test that picked a different font on
            // different machines would be a test of the machine.
            std::array<std::optional<std::filesystem::path>, CANDIDATES.size()> found{};

            for (const std::string_view root : ROOTS)
            {
                std::error_code error;
                if (!std::filesystem::is_directory(root, error))
                {
                    continue;
                }

                // The non-throwing iterator: an unreadable directory somewhere
                // under a font root must skip that directory, not abandon the
                // search and fail every case.
                std::filesystem::recursive_directory_iterator walk{
                    root, std::filesystem::directory_options::skip_permission_denied, error};
                const std::filesystem::recursive_directory_iterator end;

                for (; !error && walk != end; walk.increment(error))
                {
                    if (!walk->is_regular_file(error))
                    {
                        continue;
                    }

                    const std::string name = walk->path().filename().string();

                    for (std::size_t index = 0; index < CANDIDATES.size(); ++index)
                    {
                        if (!found[index].has_value() && name == CANDIDATES[index])
                        {
                            found[index] = walk->path();
                        }
                    }
                }
            }

            for (const std::optional<std::filesystem::path>& candidate : found)
            {
                if (candidate.has_value())
                {
                    return candidate;
                }
            }

            return std::nullopt;
        }();

        return located;
    }
}

#endif //CPEN_TESTS_SUPPORT_SYSTEM_FONT_HH
