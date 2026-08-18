#include <catch2/catch_test_macros.hpp>

#include "cpen/assets/asset_manager.hh"
#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/error.hh"
#include "cpen/core/log.hh"
#include "cpen/script/chunk.hh"
#include "support/log_capture.hh"
#include "support/temporary_directory.hh"

#include <string>
#include <string_view>

using cpen::assets::AssetManager;
using cpen::assets::AssetResolver;
using cpen::assets::ScriptReference;
using cpen::assets::VirtualFileSystem;
using cpen::core::ErrorCode;
using cpen::test::LogCaptureGuard;
using cpen::test::TemporaryDirectory;

namespace
{
    /// The whole stack below the manager, assembled the way the Application
    /// assembles it.
    struct Game
    {
        TemporaryDirectory directory;
        VirtualFileSystem files;
        AssetResolver resolver;
        AssetManager assets;

        Game() : resolver(files), assets(files, resolver)
        {
            this->files.mount(this->directory.path());
        }

        void write_script(const std::string_view identifier, const std::string_view source)
        {
            // Scripts sit beside assets/ rather than inside it: they are the
            // game, not something the game shows.
            this->directory.write(std::string{"script/"} + std::string{identifier} + ".pen",
                                  source);
        }
    };

    constexpr std::string_view A_SHORT_SCENE =
        "label start:\n"
        "\tscene room\n"
        "\t\"Привет.\"\n";
}

TEST_CASE("a script is loaded and compiled by name", "[assets][script]")
{
    Game game;
    game.write_script("intro", A_SHORT_SCENE);

    const auto loaded = game.assets.script("intro");

    REQUIRE(loaded.has_value());
    REQUIRE(static_cast<bool>(*loaded));

    // The text is kept beside the bytecode, which is what a run-time fault is
    // reported against.
    CHECK((*loaded)->source == A_SHORT_SCENE);
    CHECK((*loaded)->chunk.find_label("start").has_value());
    CHECK(game.assets.loaded_script_count() == 1);
}

TEST_CASE("the name a script is compiled under is the file it came from", "[assets][script]")
{
    Game game;
    game.write_script("intro", A_SHORT_SCENE);

    const auto loaded = game.assets.script("intro");

    REQUIRE(loaded.has_value());

    // Not the identifier the caller wrote: a heading on a diagnostic is only
    // useful if it names something a person can open.
    CHECK((*loaded)->name == "script/intro.pen");
}

TEST_CASE("a script asked for twice is compiled once", "[assets][script]")
{
    Game game;
    game.write_script("intro", A_SHORT_SCENE);

    const auto first = game.assets.script("intro");
    const auto second = game.assets.script("intro");

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    CHECK(game.assets.loaded_script_count() == 1);
    CHECK(first->get() == second->get());
}

TEST_CASE("a compiled script is unloaded once nothing holds it", "[assets][script]")
{
    Game game;
    game.write_script("intro", A_SHORT_SCENE);

    {
        const auto loaded = game.assets.script("intro");
        REQUIRE(loaded.has_value());
        CHECK(game.assets.collect_unused() == 0);
    }

    CHECK(game.assets.collect_unused() == 1);
    CHECK(game.assets.loaded_script_count() == 0);
}

TEST_CASE("a missing script is reported and recorded", "[assets][script]")
{
    Game game;
    const LogCaptureGuard capture;

    const auto loaded = game.assets.script("nowhere");

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == ErrorCode::FILE_NOT_FOUND);

    // The ledger the Application prints at the end of a run: an asset that never
    // loaded left a hole somebody may not have looked at.
    REQUIRE(game.assets.missing().size() == 1);
    CHECK(game.assets.missing()[0].identifier == "nowhere");
}

TEST_CASE("a script that does not compile fails with every complaint in it",
          "[assets][script]")
{
    Game game;
    const LogCaptureGuard capture;

    game.write_script("broken",
                      "label start:\n"
                      "\tif :\n"
                      "\t\t\"one\"\n"
                      "\t$ = 3\n");

    const auto loaded = game.assets.script("broken");

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == ErrorCode::COMPILATION_FAILED);

    // The rendered list, not the first line of it: the message names the file and
    // carries a caret line, and it says more than one thing.
    CHECK(loaded.error().message.find("script/broken.pen") != std::string::npos);
    CHECK(loaded.error().message.find('^') != std::string::npos);
}

TEST_CASE("a script that failed to compile is not cached as loaded", "[assets][script]")
{
    Game game;
    const LogCaptureGuard capture;

    game.write_script("broken", "label start:\n\tif :\n\t\t\"one\"\n");

    REQUIRE_FALSE(game.assets.script("broken").has_value());
    CHECK(game.assets.loaded_script_count() == 0);
}
