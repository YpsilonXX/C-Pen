#ifndef CPEN_TESTS_SUPPORT_ENGINE_FONT_HH
#define CPEN_TESTS_SUPPORT_ENGINE_FONT_HH

#include <catch2/catch_test_macros.hpp>

#include "cpen/app/asset_roots.hh"
#include "cpen/core/file_system.hh"

#include <filesystem>

namespace cpen::test
{
    /// The typeface the engine ships, as staged beside this test binary.
    ///
    /// Replaces the borrowing of a system typeface these cases used to do, and the
    /// SKIP that went with it. Borrowing was right while the engine shipped no
    /// font: a machine without one was a machine the tests could say nothing
    /// about. It is wrong now, and in a specific way — a skipped font case on a
    /// build machine is a green run that exercised nothing, which is exactly the
    /// report you least want. The engine ships a typeface; if it is not here, that
    /// is a failure of the build and every game built with it is broken too.
    ///
    /// Found the way a game finds it: the roots beside the executable, which is
    /// also what proves the staging works.
    inline std::filesystem::path engine_font_path()
    {
        const auto roots = app::default_asset_roots();
        REQUIRE(roots.has_value());

        std::filesystem::path path =
            roots->engine / core::path_from_utf8("assets/fonts/default.ttf");

        INFO("the engine's typeface must be staged beside the test binary: "
             << path.string());
        REQUIRE(std::filesystem::is_regular_file(path));

        return path;
    }
}

#endif //CPEN_TESTS_SUPPORT_ENGINE_FONT_HH
