#include <catch2/catch_test_macros.hpp>

#include "cpen/app/game_manifest.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/error.hh"
#include "cpen/core/log.hh"
#include "cpen/render/viewport.hh"
#include "support/log_capture.hh"
#include "support/temporary_directory.hh"

#include <string>
#include <string_view>

using cpen::app::GameManifest;
using cpen::app::configuration_from_manifest;
using cpen::core::ErrorCode;
using cpen::render::ScaleMode;
using cpen::test::LogCaptureGuard;
using cpen::test::TemporaryDirectory;

namespace
{
    GameManifest parsed(const std::string_view text)
    {
        auto manifest = GameManifest::parse(text, "game.toml");
        REQUIRE(manifest.has_value());
        return std::move(*manifest);
    }

    constexpr std::string_view A_WHOLE_MANIFEST =
        "name = \"Демо\"\n"
        "version = \"0.2.0\"\n"
        "\n"
        "[window]\n"
        "title = \"Демо — C-Pen\"\n"
        "width = 1600\n"
        "height = 900\n"
        "\n"
        "[screen]\n"
        "width = 2560\n"
        "height = 1440\n"
        "scale = \"stretch\"\n"
        "\n"
        "[story]\n"
        "script = \"intro\"\n"
        "label = \"chapter_one\"\n";
}

TEST_CASE("a manifest is read whole", "[app][manifest]")
{
    const GameManifest manifest = parsed(A_WHOLE_MANIFEST);

    CHECK(manifest.name == "Демо");
    CHECK(manifest.version == "0.2.0");

    CHECK(manifest.window.title == "Демо — C-Pen");
    CHECK(manifest.window.width == 1600);
    CHECK(manifest.window.height == 900);

    CHECK(manifest.screen.width == 2560);
    CHECK(manifest.screen.height == 1440);
    CHECK(manifest.screen.scale_mode == ScaleMode::STRETCH);

    CHECK(manifest.story.script == "intro");
    CHECK(manifest.story.label == "chapter_one");
}

TEST_CASE("an empty manifest is every default", "[app][manifest]")
{
    const GameManifest manifest = parsed("");

    CHECK(manifest.name.empty());
    CHECK(manifest.window.width == 1280);
    CHECK(manifest.screen.width == cpen::render::Viewport::DEFAULT_VIRTUAL_WIDTH);
    CHECK(manifest.screen.scale_mode == ScaleMode::LETTERBOX);
    CHECK(manifest.story.script.empty());
}

TEST_CASE("a game with no window title of its own wears its name", "[app][manifest]")
{
    const GameManifest manifest = parsed("name = \"Демо\"\n");

    CHECK(manifest.window.title == "Демо");
}

TEST_CASE("a title that was written wins over the name", "[app][manifest]")
{
    const GameManifest manifest = parsed("name = \"Демо\"\n[window]\ntitle = \"Другое\"\n");

    CHECK(manifest.window.title == "Другое");
}

TEST_CASE("a file that is not TOML at all is refused", "[app][manifest]")
{
    const auto manifest = GameManifest::parse("name = = =\n", "game.toml");

    REQUIRE_FALSE(manifest.has_value());
    CHECK(manifest.error().code == ErrorCode::INVALID_FORMAT);

    // The heading of the complaint is something a person can open, and it says
    // where in the file to look.
    CHECK(manifest.error().message.find("game.toml:1:") != std::string::npos);
}

TEST_CASE("a value of the wrong type is reported and the default kept",
          "[app][manifest]")
{
    const LogCaptureGuard capture;

    const GameManifest manifest = parsed("[window]\nwidth = \"big\"\n");

    CHECK(manifest.window.width == 1280);
    CHECK(capture.count(cpen::log::Level::WARN) == 1);
}

TEST_CASE("a resolution of zero is refused like the wrong type", "[app][manifest]")
{
    const LogCaptureGuard capture;

    // Every quantity derived from a resolution divides by it, so this one is not
    // a taste the manifest is allowed to express.
    const GameManifest manifest = parsed("[screen]\nwidth = 0\n");

    CHECK(manifest.screen.width == cpen::render::Viewport::DEFAULT_VIRTUAL_WIDTH);
    CHECK(capture.count(cpen::log::Level::WARN) == 1);
}

TEST_CASE("a scale mode nobody implements is reported", "[app][manifest]")
{
    const LogCaptureGuard capture;

    const GameManifest manifest = parsed("[screen]\nscale = \"crop\"\n");

    CHECK(manifest.screen.scale_mode == ScaleMode::LETTERBOX);
    CHECK(capture.count(cpen::log::Level::WARN) == 1);
}

TEST_CASE("a key nobody reads is reported", "[app][manifest]")
{
    const LogCaptureGuard capture;

    // A manifest is edited by hand and never compiled, so a misspelt key is
    // otherwise perfectly silent.
    const GameManifest manifest = parsed("[story]\nstrat = \"intro\"\n");

    CHECK(manifest.story.script.empty());
    CHECK(capture.count(cpen::log::Level::WARN) == 1);
    CHECK(capture.contains("story.strat"));
}

TEST_CASE("a section written as a value is reported, not read", "[app][manifest]")
{
    const LogCaptureGuard capture;

    const GameManifest manifest = parsed("window = \"big\"\n");

    CHECK(manifest.window.width == 1280);
    CHECK(capture.count(cpen::log::Level::WARN) == 1);
}

TEST_CASE("a manifest is read through the file system", "[app][manifest]")
{
    TemporaryDirectory directory;
    directory.write("game.toml", A_WHOLE_MANIFEST);

    cpen::assets::VirtualFileSystem files;
    files.mount(directory.path());

    const auto manifest = GameManifest::read(files);

    REQUIRE(manifest.has_value());
    CHECK(manifest->story.script == "intro");
}

TEST_CASE("a game with no manifest says so", "[app][manifest]")
{
    TemporaryDirectory directory;

    cpen::assets::VirtualFileSystem files;
    files.mount(directory.path());

    const auto manifest = GameManifest::read(files);

    REQUIRE_FALSE(manifest.has_value());
    CHECK(manifest.error().code == ErrorCode::FILE_NOT_FOUND);
}

TEST_CASE("the manifest becomes the settings the application starts from",
          "[app][manifest]")
{
    const GameManifest manifest = parsed(A_WHOLE_MANIFEST);

    const auto configuration = configuration_from_manifest(
        manifest, cpen::app::AssetRoots{.game = "/games/demo", .engine = "/engine"});

    CHECK(configuration.window.title == "Демо — C-Pen");
    CHECK(configuration.window.width == 1600);
    CHECK(configuration.virtual_width == 2560);
    CHECK(configuration.scale_mode == ScaleMode::STRETCH);

    REQUIRE(configuration.roots.has_value());
    CHECK(configuration.roots->game == "/games/demo");
}
