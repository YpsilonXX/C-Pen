#include <catch2/catch_test_macros.hpp>

#include "cpen/app/asset_roots.hh"
#include "cpen/core/error.hh"
#include "cpen/platform/executable_path.hh"
#include "support/trace.hh"

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

using cpen::app::apply_asset_root_arguments;
using cpen::app::AssetRoots;
using cpen::app::asset_roots_from_command_line;
using cpen::app::default_asset_roots;
using cpen::core::ErrorCode;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    AssetRoots given()
    {
        return AssetRoots{.game = "/opt/game", .engine = "/opt/engine"};
    }

    AssetRoots applied(const std::vector<std::string_view>& arguments)
    {
        const auto roots = apply_asset_root_arguments(arguments, given());
        REQUIRE(roots.has_value());
        return *roots;
    }
}

TEST_CASE("the roots default to the directory the program is in", "[app][asset_roots]")
{
    const auto roots = default_asset_roots();
    REQUIRE(roots.has_value());

    const auto directory = cpen::platform::executable_directory();
    REQUIRE(directory.has_value());

    trace("game root:   {}", roots->game.string());
    trace("engine root: {}", roots->engine.string());

    CHECK(roots->game == *directory / "game");
    CHECK(roots->engine == *directory / "engine");

    // The engine's own root is staged beside every binary the build produces, so
    // this one really is there — which is also what the font cases depend on.
    trace_step("and the engine's root really is beside this binary");
    CHECK(std::filesystem::is_directory(roots->engine));
}

TEST_CASE("an override replaces one root and leaves the other", "[app][asset_roots]")
{
    const AssetRoots roots = applied({"--game", "/srv/novel"});

    CHECK(roots.game == std::filesystem::path{"/srv/novel"});
    CHECK(roots.engine == std::filesystem::path{"/opt/engine"});

    trace_step("both spellings, because both are what people type");
    CHECK(applied({"--game=/srv/novel"}).game == std::filesystem::path{"/srv/novel"});

    trace_step("and the engine's root can be moved the same way");
    CHECK(applied({"--engine", "/srv/cpen"}).engine == std::filesystem::path{"/srv/cpen"});
}

TEST_CASE("what the engine does not recognise, it leaves alone", "[app][asset_roots]")
{
    // A game is entitled to its own command line. Refusing an argument the engine
    // does not know would make every game's options the engine's business.
    const AssetRoots roots = applied({"--fullscreen", "--language", "ru", "--game",
                                      "/srv/novel", "--skip-intro"});

    CHECK(roots.game == std::filesystem::path{"/srv/novel"});

    trace_step("an option that merely begins like one of ours is not one of ours");
    CHECK(applied({"--gamepad", "xbox"}).game == std::filesystem::path{"/opt/game"});
}

TEST_CASE("an override with nothing after it is refused", "[app][asset_roots]")
{
    for (const std::vector<std::string_view>& arguments :
         {std::vector<std::string_view>{"--game"},
          std::vector<std::string_view>{"--engine"},
          std::vector<std::string_view>{"--game="}})
    {
        const auto roots = apply_asset_root_arguments(arguments, given());

        REQUIRE_FALSE(roots.has_value());
        trace("{}", roots.error());

        // Refused rather than ignored: silently keeping the default means the game
        // starts on the wrong data and reports every asset missing, which is a
        // long way from the typo that caused it.
        CHECK(roots.error().code == ErrorCode::INVALID_FORMAT);
    }
}

TEST_CASE("a relative override means what the terminal means", "[app][asset_roots]")
{
    const auto roots = apply_asset_root_arguments(
        std::array<std::string_view, 2>{"--game", "."}, given());

    REQUIRE(roots.has_value());
    trace("'.' became {}", roots->game.string());

    // Resolved against the working directory rather than the executable: somebody
    // typing a path sees their shell's idea of where they are, not the engine's.
    CHECK(roots->game == std::filesystem::current_path());
}

TEST_CASE("a command line with no options is the default layout", "[app][asset_roots]")
{
    const std::array<const char*, 1> arguments = {"cpen_demo"};

    const auto roots = asset_roots_from_command_line(1, arguments.data());
    const auto defaults = default_asset_roots();

    REQUIRE(roots.has_value());
    REQUIRE(defaults.has_value());

    CHECK(roots->game == defaults->game);
    CHECK(roots->engine == defaults->engine);
}
