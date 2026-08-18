#include <catch2/catch_test_macros.hpp>

#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/error.hh"
#include "cpen/core/log.hh"
#include "support/log_capture.hh"
#include "support/temporary_directory.hh"
#include "support/trace.hh"

#include <string>
#include <string_view>
#include <vector>

using cpen::assets::AssetKind;
using cpen::assets::AssetResolver;
using cpen::assets::compose_virtual_path;
using cpen::assets::directory_of;
using cpen::assets::extensions_of;
using cpen::assets::validate_asset_id;
using cpen::assets::VirtualFileSystem;
using cpen::core::ErrorCode;
using cpen::test::LogCaptureGuard;
using cpen::test::TemporaryDirectory;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    /// A game directory with a file system already mounted over it.
    struct Game
    {
        TemporaryDirectory directory;
        VirtualFileSystem files;

        Game()
        {
            this->files.set_path_audit(true);
            this->files.mount(this->directory.path());
        }
    };
}

TEST_CASE("where each kind of asset lives", "[assets][resolver]")
{
    CHECK(directory_of(AssetKind::BACKGROUND) == "assets/bg");
    CHECK(directory_of(AssetKind::SPRITE) == "assets/sprites");
    CHECK(directory_of(AssetKind::FONT) == "assets/fonts");
    CHECK(directory_of(AssetKind::AUDIO) == "assets/audio");

    trace_step("scripts are the game, not something the game shows");
    CHECK(directory_of(AssetKind::SCRIPT) == "script");

    trace_step("lossless before lossy, so a converted picture wins over the original");
    REQUIRE(extensions_of(AssetKind::SPRITE).size() >= 2);
    CHECK(extensions_of(AssetKind::SPRITE)[0] == "png");
}

TEST_CASE("composing a path from an identifier", "[assets][resolver]")
{
    CHECK(compose_virtual_path(AssetKind::SPRITE, "alice/happy", "png") ==
          "assets/sprites/alice/happy.png");

    CHECK(compose_virtual_path(AssetKind::SCRIPT, "chapter_01", "pen") ==
          "script/chapter_01.pen");

    trace_step("a caller who writes the dot means the same thing");
    CHECK(compose_virtual_path(AssetKind::BACKGROUND, "room", ".png") == "assets/bg/room.png");
}

TEST_CASE("an identifier resolves by convention alone", "[assets][resolver]")
{
    Game game;
    game.directory.write("assets/sprites/alice/happy.png", "pixels");
    game.directory.write("assets/bg/room.jpg", "pixels");
    game.directory.write("assets/fonts/ui.ttf", "glyphs");
    game.directory.write("script/chapter_01.pen", "label start\n");

    const AssetResolver resolver(game.files);

    // No registration, no manifest entry, no build step: a new sprite is a new
    // file in the right directory.
    CHECK(*resolver.resolve(AssetKind::SPRITE, "alice/happy") ==
          "assets/sprites/alice/happy.png");

    trace_step("the extension is found, not spelled — this one is not even the first tried");
    CHECK(*resolver.resolve(AssetKind::BACKGROUND, "room") == "assets/bg/room.jpg");

    CHECK(*resolver.resolve(AssetKind::FONT, "ui") == "assets/fonts/ui.ttf");
    CHECK(*resolver.resolve(AssetKind::SCRIPT, "chapter_01") == "script/chapter_01.pen");
}

TEST_CASE("one name may mean two things of different kinds", "[assets][resolver]")
{
    Game game;
    game.directory.write("assets/bg/alice.png", "her bedroom");
    game.directory.write("assets/sprites/alice.png", "her portrait");

    const AssetResolver resolver(game.files);

    CHECK(*resolver.resolve(AssetKind::BACKGROUND, "alice") == "assets/bg/alice.png");
    CHECK(*resolver.resolve(AssetKind::SPRITE, "alice") == "assets/sprites/alice.png");
}

TEST_CASE("a missing asset lists every path that was tried", "[assets][resolver]")
{
    Game game;
    const AssetResolver resolver(game.files);

    const auto resolved = resolver.resolve(AssetKind::SPRITE, "alice/happy");

    REQUIRE_FALSE(resolved.has_value());
    trace("{}", resolved.error());

    CHECK(resolved.error().code == ErrorCode::FILE_NOT_FOUND);

    // The author has to be able to see the convention they missed, not
    // reconstruct it from memory — which they will do wrongly in exactly the way
    // that caused this.
    CHECK(resolved.error().message.find("assets/sprites/alice/happy.png") != std::string::npos);
    CHECK(resolved.error().message.find("assets/sprites/alice/happy.jpg") != std::string::npos);
}

TEST_CASE("two files answering to one identifier are reported", "[assets][resolver]")
{
    Game game;
    game.directory.write("assets/bg/room.png", "the new one");
    game.directory.write("assets/bg/room.jpg", "the one that was converted");

    const AssetResolver resolver(game.files);
    const LogCaptureGuard log_capture;

    CHECK(*resolver.resolve(AssetKind::BACKGROUND, "room") == "assets/bg/room.png");

    // Deterministic, so not a failure — but an author editing the JPEG is editing
    // a file the game never opens, and nothing else would tell them.
    CHECK(log_capture.count(cpen::log::Level::WARN) == 1);
    CHECK(log_capture.contains("assets/bg/room.jpg"));

    trace_step("and reported once, not once per load");
    CHECK(*resolver.resolve(AssetKind::BACKGROUND, "room") == "assets/bg/room.png");
    CHECK(log_capture.count(cpen::log::Level::WARN) == 1);
}

TEST_CASE("an identifier carrying an extension is refused", "[assets][resolver]")
{
    Game game;
    game.directory.write("assets/sprites/alice.png", "pixels");

    const AssetResolver resolver(game.files);

    const auto resolved = resolver.resolve(AssetKind::SPRITE, "alice.png");

    REQUIRE_FALSE(resolved.has_value());
    trace("{}", resolved.error());

    CHECK(resolved.error().code == ErrorCode::INVALID_FORMAT);

    // Refused rather than accepted, because accepting it would send the resolver
    // looking for "alice.png.png" and report a missing file that is right there.
    CHECK(resolved.error().message.find("'alice'") != std::string::npos);

    trace_step("a dot that is not an extension is an ordinary character");
    CHECK(validate_asset_id(AssetKind::SPRITE, "alice.happy").has_value());
}

TEST_CASE("an identifier that cannot name a file is refused", "[assets][resolver]")
{
    for (const std::string_view identifier : {"", "../secret", "sprites\\alice", "alice/"})
    {
        const auto valid = validate_asset_id(AssetKind::SPRITE, identifier);

        REQUIRE_FALSE(valid.has_value());
        trace("'{}' -> {}", identifier, valid.error().message);
    }
}

TEST_CASE("an alias overrides the convention", "[assets][resolver]")
{
    Game game;
    game.directory.write("assets/sprites/alice/happy.png", "the new artwork");
    game.directory.write("assets/legacy/alice_v1.png", "the artwork actually wanted");

    AssetResolver resolver(game.files);

    REQUIRE(resolver.add_alias(AssetKind::SPRITE, "alice/happy",
                               "assets/legacy/alice_v1.png").has_value());

    CHECK(*resolver.resolve(AssetKind::SPRITE, "alice/happy") == "assets/legacy/alice_v1.png");

    trace_step("and only for the kind it was registered for");
    game.directory.write("assets/bg/alice/happy.png", "a background");
    CHECK(*resolver.resolve(AssetKind::BACKGROUND, "alice/happy") ==
          "assets/bg/alice/happy.png");
}

TEST_CASE("an alias is checked when it is registered and when it is used",
          "[assets][resolver]")
{
    Game game;
    AssetResolver resolver(game.files);

    trace_step("a manifest is typed by hand, so a bad path is refused on the spot");
    const auto added = resolver.add_alias(AssetKind::AUDIO, "theme", "../outside/theme.ogg");

    REQUIRE_FALSE(added.has_value());
    trace("{}", added.error());
    CHECK(added.error().code == ErrorCode::INVALID_FORMAT);

    trace_step("a well-formed alias to a file that is not there fails at use");
    REQUIRE(resolver.add_alias(AssetKind::AUDIO, "theme", "assets/audio/legacy/theme.ogg")
                .has_value());

    const auto resolved = resolver.resolve(AssetKind::AUDIO, "theme");

    REQUIRE_FALSE(resolved.has_value());
    trace("{}", resolved.error());

    // Naming the alias matters: without it the message describes a path the
    // author never wrote and cannot find in their manifest.
    CHECK(resolved.error().message.find("aliased") != std::string::npos);
}
