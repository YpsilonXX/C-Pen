#include <catch2/catch_test_macros.hpp>

#include "cpen/app/asset_roots.hh"
#include "cpen/assets/asset_manager.hh"
#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/error.hh"
#include "cpen/core/file_system.hh"
#include "cpen/render/pixel_format.hh"
#include "support/gl_fixture.hh"
#include "support/engine_font.hh"
#include "support/temporary_directory.hh"
#include "support/test_image.hh"
#include "support/trace.hh"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using cpen::assets::AssetKind;
using cpen::assets::AssetManager;
using cpen::assets::AssetResolver;
using cpen::assets::FontReference;
using cpen::assets::TextureReference;
using cpen::assets::VirtualFileSystem;
using cpen::core::ErrorCode;
using cpen::test::engine_font_path;
using cpen::test::gl_context;
using cpen::test::TemporaryDirectory;
using cpen::test::tiny_png_text;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
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
}

TEST_CASE("a picture becomes a texture on the driver", "[assets][manager][gpu]")
{
    static_cast<void>(gl_context());

    Game game;
    game.directory.write("assets/bg/room.png", tiny_png_text());

    const auto texture = game.assets.texture(AssetKind::BACKGROUND, "room");

    REQUIRE(texture.has_value());
    REQUIRE(static_cast<bool>(*texture));

    CHECK((*texture)->width() == 2);
    CHECK((*texture)->height() == 2);
    CHECK((*texture)->format() == cpen::render::PixelFormat::RGBA8);

    trace_step("asked for again, it is the same texture and no second upload");
    const auto again = game.assets.texture(AssetKind::BACKGROUND, "room");
    REQUIRE(again.has_value());

    CHECK(again->handle() == texture->handle());
    CHECK(game.assets.loaded_texture_count() == 1);

    trace_step("and the pixels it was made from are not kept");
    CHECK(game.assets.loaded_image_count() == 0);
}

TEST_CASE("the placeholder texture is there when the asset is not",
          "[assets][manager][gpu]")
{
    static_cast<void>(gl_context());

    Game game;

    const auto missing = game.assets.texture(AssetKind::SPRITE, "alice/happy");
    REQUIRE_FALSE(missing.has_value());

    const cpen::render::Texture* placeholder = game.assets.placeholder_texture();

    REQUIRE(placeholder != nullptr);
    CHECK(placeholder->width() == 64);
    CHECK(placeholder->height() == 64);

    trace_step("the same one every time: it is not an asset and cannot be collected");
    CHECK(game.assets.placeholder_texture() == placeholder);
    CHECK(game.assets.collect_unused() == 0);
    CHECK(game.assets.placeholder_texture() == placeholder);
}

TEST_CASE("a typeface loads through the asset layer, once per size",
          "[assets][manager][gpu]")
{
    static_cast<void>(gl_context());

    Game game;

    const auto data = cpen::core::read_file_bytes(engine_font_path());
    REQUIRE(data.has_value());
    game.directory.write("assets/fonts/ui.ttf", *data);

    const auto small = game.assets.font("ui", 24);
    REQUIRE(small.has_value());
    trace("loaded '{}' at 24 pixels", (*small)->family_name());

    CHECK((*small)->pixel_size() == 24);

    trace_step("the same size is the same Font");
    const auto same = game.assets.font("ui", 24);
    REQUIRE(same.has_value());
    CHECK(same->handle() == small->handle());

    // A Font is one typeface at one size with one atlas, so another size is
    // another asset — not another way of drawing this one.
    trace_step("another size is another asset, with an atlas of its own");
    const auto large = game.assets.font("ui", 48);
    REQUIRE(large.has_value());

    CHECK_FALSE(large->handle() == small->handle());
    CHECK(game.assets.loaded_font_count() == 2);
    CHECK((*large)->pixel_size() == 48);
}

TEST_CASE("the engine's own typeface loads out of the root it ships in",
          "[assets][manager][gpu]")
{
    static_cast<void>(gl_context());

    const auto roots = cpen::app::default_asset_roots();
    REQUIRE(roots.has_value());

    // The real engine root beside this binary, not a temporary one: this case is
    // the end-to-end proof that a game which ships nothing at all still has a
    // typeface, and that the staging which puts it there works.
    VirtualFileSystem files;
    files.mount(roots->engine);

    const AssetResolver engine_resolver(files);
    AssetManager engine_assets(files, engine_resolver);

    const auto font = engine_assets.default_font(24);

    REQUIRE(font.has_value());
    trace("the engine ships '{}'", (*font)->family_name());

    CHECK((*font)->pixel_size() == 24);
    CHECK((*font)->family_name() == "DejaVu Sans");

    // Cyrillic is the reason this face was chosen over a smaller one, so it is
    // worth an assertion rather than a comment.
    const cpen::render::Glyph* cyrillic = (*font)->glyph(U'Я');

    REQUIRE(cyrillic != nullptr);
    CHECK(cyrillic->advance > 0.0f);
}

TEST_CASE("a file that is not a typeface fails as one", "[assets][manager][gpu]")
{
    static_cast<void>(gl_context());

    Game game;
    game.directory.write("assets/fonts/ui.ttf", tiny_png_text());

    const auto font = game.assets.font("ui", 24);

    REQUIRE_FALSE(font.has_value());
    trace("{}", font.error());

    CHECK(font.error().code == ErrorCode::INVALID_FORMAT);

    // The message has to name the file. FreeType's own error says only that
    // something was not a face.
    CHECK(font.error().message.find("assets/fonts/ui.ttf") != std::string::npos);
}

TEST_CASE("textures and fonts are collected like everything else",
          "[assets][manager][gpu]")
{
    static_cast<void>(gl_context());

    Game game;
    game.directory.write("assets/bg/room.png", tiny_png_text());

    {
        const auto texture = game.assets.texture(AssetKind::BACKGROUND, "room");
        REQUIRE(texture.has_value());
    }

    CHECK(game.assets.loaded_texture_count() == 1);
    CHECK(game.assets.collect_unused() == 1);
    CHECK(game.assets.loaded_texture_count() == 0);
}
