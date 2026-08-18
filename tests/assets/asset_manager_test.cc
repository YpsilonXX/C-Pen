#include <catch2/catch_test_macros.hpp>

#include "cpen/assets/asset_manager.hh"
#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/placeholder.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/error.hh"
#include "cpen/core/log.hh"
#include "cpen/render/pixel_format.hh"
#include "support/log_capture.hh"
#include "support/temporary_directory.hh"
#include "support/test_image.hh"
#include "support/trace.hh"

#include <cstddef>
#include <cstdint>
#include <string>

using cpen::assets::AssetKind;
using cpen::assets::AssetManager;
using cpen::assets::AssetResolver;
using cpen::assets::ImageReference;
using cpen::assets::make_placeholder_image;
using cpen::assets::VirtualFileSystem;
using cpen::core::ErrorCode;
using cpen::test::LogCaptureGuard;
using cpen::test::TemporaryDirectory;
using cpen::test::tiny_png_text;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    /// A game directory, mounted, resolved and managed — the whole stack below
    /// the manager, assembled the way the Application will assemble it.
    struct Game
    {
        TemporaryDirectory directory;
        VirtualFileSystem files;
        AssetResolver resolver;
        AssetManager assets;

        Game() : resolver(files), assets(files, resolver)
        {
            this->files.set_path_audit(true);
            this->files.mount(this->directory.path());
        }
    };

    std::uint8_t channel(const cpen::render::Image& image, const std::size_t index)
    {
        return static_cast<std::uint8_t>(image.pixels()[index]);
    }
}

TEST_CASE("the manager is usable with no graphics context", "[assets][manager]")
{
    // The whole reason images, identifiers, caching and the ledger are reachable
    // without a driver: this object goes into GameContext, and the state stack's
    // tests must keep running without a window.
    Game game;
    game.directory.write("assets/sprites/alice/happy.png", tiny_png_text());

    const auto image = game.assets.image(AssetKind::SPRITE, "alice/happy");

    REQUIRE(image.has_value());
    REQUIRE(static_cast<bool>(*image));

    CHECK((*image)->width() == 2);
    CHECK((*image)->height() == 2);
    CHECK((*image)->format() == cpen::render::PixelFormat::RGBA8);

    trace_step("and the pixels really are the file's, in the file's row order");
    CHECK(channel(**image, 0) == 220);
    CHECK(channel(**image, 15) == 128);
}

TEST_CASE("an asset asked for twice is loaded once", "[assets][manager]")
{
    Game game;
    game.directory.write("assets/bg/room.png", tiny_png_text());

    const auto first = game.assets.image(AssetKind::BACKGROUND, "room");
    const auto second = game.assets.image(AssetKind::BACKGROUND, "room");

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    CHECK(first->handle() == second->handle());
    CHECK(first->get() == second->get());
    CHECK(game.assets.loaded_image_count() == 1);
}

TEST_CASE("two identifiers for one file are one asset", "[assets][manager]")
{
    Game game;
    game.directory.write("assets/sprites/alice/happy.png", tiny_png_text());

    REQUIRE(game.resolver.add_alias(AssetKind::SPRITE, "alice/smiling",
                                    "assets/sprites/alice/happy.png").has_value());

    const auto happy = game.assets.image(AssetKind::SPRITE, "alice/happy");
    const auto smiling = game.assets.image(AssetKind::SPRITE, "alice/smiling");

    REQUIRE(happy.has_value());
    REQUIRE(smiling.has_value());

    // Keyed by the file they resolve to, not by the name that was asked for: the
    // alternative is the same picture in memory twice because the manifest gave
    // it a second name.
    CHECK(happy->handle() == smiling->handle());
    CHECK(game.assets.loaded_image_count() == 1);
}

TEST_CASE("a missing asset is an error, not a substitute", "[assets][manager]")
{
    Game game;
    const LogCaptureGuard log_capture;

    const auto image = game.assets.image(AssetKind::SPRITE, "alice/furious");

    REQUIRE_FALSE(image.has_value());
    trace("{}", image.error());

    CHECK(image.error().code == ErrorCode::FILE_NOT_FOUND);

    trace_step("recorded once, however often it is asked for");
    static_cast<void>(game.assets.image(AssetKind::SPRITE, "alice/furious"));

    REQUIRE(game.assets.missing().size() == 1);
    CHECK(log_capture.count(cpen::log::Level::ERROR) == 1);
}

TEST_CASE("the missing-asset ledger survives to the end of the run", "[assets][manager]")
{
    Game game;
    const LogCaptureGuard log_capture;

    static_cast<void>(game.assets.image(AssetKind::SPRITE, "alice/furious"));
    static_cast<void>(game.assets.image(AssetKind::BACKGROUND, "hall"));

    REQUIRE(game.assets.missing().size() == 2);
    CHECK(game.assets.missing()[0].kind == AssetKind::SPRITE);
    CHECK(game.assets.missing()[0].identifier == "alice/furious");

    const std::string summary = game.assets.format_missing_summary();
    trace("{}", summary);

    // A failure in minute three of a playtest is a thousand log lines above the
    // end of the run. This is what makes it findable.
    CHECK(summary.find("alice/furious") != std::string::npos);
    CHECK(summary.find("hall") != std::string::npos);

    CHECK(log_capture.count(cpen::log::Level::ERROR) == 2);
}

TEST_CASE("a released asset is kept until the game says otherwise", "[assets][manager]")
{
    Game game;
    game.directory.write("assets/bg/room.png", tiny_png_text());

    {
        const auto image = game.assets.image(AssetKind::BACKGROUND, "room");
        REQUIRE(image.has_value());
        CHECK(game.assets.loaded_image_count() == 1);
    }

    trace_step("out of scope, out of references — and still loaded");
    CHECK(game.assets.loaded_image_count() == 1);

    trace_step("a second showing costs nothing, which is the point");
    const auto again = game.assets.image(AssetKind::BACKGROUND, "room");
    REQUIRE(again.has_value());

    trace_step("and while it is held, collection leaves it alone");
    CHECK(game.assets.collect_unused() == 0);
    CHECK(game.assets.loaded_image_count() == 1);
}

TEST_CASE("collection unloads what nothing holds", "[assets][manager]")
{
    Game game;
    game.directory.write("assets/bg/room.png", tiny_png_text());
    game.directory.write("assets/bg/hall.png", tiny_png_text());

    ImageReference held;

    {
        const auto room = game.assets.image(AssetKind::BACKGROUND, "room");
        const auto hall = game.assets.image(AssetKind::BACKGROUND, "hall");

        REQUIRE(room.has_value());
        REQUIRE(hall.has_value());

        held = *hall;
    }

    CHECK(game.assets.loaded_image_count() == 2);

    CHECK(game.assets.collect_unused() == 1);
    CHECK(game.assets.loaded_image_count() == 1);

    trace_step("what is still held is still there, and still itself");
    REQUIRE(static_cast<bool>(held));
    CHECK(held->width() == 2);
}

TEST_CASE("the placeholder picture needs no file and no driver", "[assets][manager]")
{
    const cpen::render::Image placeholder = make_placeholder_image(16, 16, 4);

    CHECK(placeholder.width() == 16);
    CHECK(placeholder.height() == 16);
    CHECK(placeholder.format() == cpen::render::PixelFormat::RGBA8);

    trace_step("magenta at the top left, dark in the next cell along");
    CHECK(channel(placeholder, 0) == 255);
    CHECK(channel(placeholder, 1) == 0);
    CHECK(channel(placeholder, 2) == 220);

    const std::size_t next_cell = 4 * 4;
    CHECK(channel(placeholder, next_cell) == 24);

    trace_step("opaque throughout: a transparent placeholder is an empty screen");
    CHECK(channel(placeholder, 3) == 255);
    CHECK(channel(placeholder, next_cell + 3) == 255);

    trace_step("a degenerate request still yields a picture rather than nothing");
    const cpen::render::Image smallest = make_placeholder_image(0, 0, 0);
    CHECK(smallest.width() == 1);
    CHECK(smallest.height() == 1);
}
