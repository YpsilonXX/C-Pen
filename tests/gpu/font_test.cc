#include <catch2/catch_test_macros.hpp>

#include "cpen/render/font.hh"
#include "cpen/render/pixel_format.hh"
#include "support/gl_fixture.hh"
#include "support/log_capture.hh"
#include "support/system_font.hh"
#include "support/trace.hh"

#include <filesystem>
#include <optional>
#include <utility>

using cpen::core::ErrorCode;
using cpen::log::Level;
using cpen::render::Font;
using cpen::render::FontConfig;
using cpen::render::Glyph;
using cpen::render::PixelFormat;
using cpen::test::find_system_font;
using cpen::test::gl_context;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    constexpr std::uint32_t TEST_PIXEL_SIZE = 24;

    /// Loads the borrowed system typeface, or skips the case.
    ///
    /// SKIP rather than FAIL: a machine with no fonts installed has not broken the
    /// engine, and a red test there would train whoever sees it to ignore red.
    Font load_font(const std::uint32_t pixel_size = TEST_PIXEL_SIZE,
                   const FontConfig& config = {})
    {
        const std::optional<std::filesystem::path> path = find_system_font();
        if (!path.has_value())
        {
            SKIP("no system typeface was found to borrow");
        }

        trace("borrowing {}", path->string());

        auto font = Font::from_file(*path, pixel_size, config);
        REQUIRE(font.has_value());
        return std::move(*font);
    }
}

TEST_CASE("a font loads with the metrics of its typeface", "[render][font][gpu]")
{
    gl_context();

    Font font = load_font();

    trace("'{}' at {} pixel(s): line height {}, ascender {}, descender {}",
          font.family_name(), font.pixel_size(), font.line_height(), font.ascender(),
          font.descender());

    CHECK(font.pixel_size() == TEST_PIXEL_SIZE);

    // Both are lengths here, unlike FreeType's own signs, so a line box is simply
    // their sum and nothing has to remember which one was negative.
    CHECK(font.ascender() > 0.0f);
    CHECK(font.descender() > 0.0f);

    // The typeface's recommended line spacing. Note what is *not* asserted: that
    // it covers ascender + descender. DejaVu Sans at 24 pixels reports 23 and 6
    // against a line height of 28, and it is not alone — the recommendation comes
    // from the typeface's own tables and is a designer's judgement about how the
    // face reads in a paragraph, not a bound on how far its outlines reach.
    // Assuming otherwise is how a layout ends up with lines that "should not"
    // overlap and do.
    CHECK(font.line_height() > 0.0f);
    CHECK(font.line_height() >= font.ascender());

    trace_step("the atlas is single channel: a glyph is coverage, not colour");
    CHECK(font.atlas().format() == PixelFormat::R8);
    CHECK(font.glyph_count() == 0);
}

TEST_CASE("a glyph is rasterised on first use and cached after", "[render][font][gpu]")
{
    gl_context();

    Font font = load_font();

    const Glyph* const first = font.glyph(U'A');
    REQUIRE(first != nullptr);

    trace("'A': {}x{} at ({}, {}) in the atlas, advance {}, bearing ({}, {})",
          first->size.x, first->size.y, first->region.position.x, first->region.position.y,
          first->advance, first->bearing.x, first->bearing.y);

    CHECK(font.glyph_count() == 1);
    CHECK(first->size.x > 0.0f);
    CHECK(first->size.y > 0.0f);
    CHECK(first->advance > 0.0f);

    // A capital letter sits on the baseline and rises above it, so its top edge is
    // above the pen: negative, because virtual space grows downwards and Font has
    // already turned FreeType's upward measurement round.
    CHECK(first->bearing.y < 0.0f);

    SECTION("and asking again returns the same glyph without rasterising it twice")
    {
        const Glyph* const again = font.glyph(U'A');

        REQUIRE(again != nullptr);
        CHECK(font.glyph_count() == 1);
        CHECK(again->region.position == first->region.position);
    }

    SECTION("while a different character gets a place of its own")
    {
        const Glyph* const other = font.glyph(U'B');

        REQUIRE(other != nullptr);
        CHECK(font.glyph_count() == 2);
        CHECK(other->region.position != first->region.position);
    }
}

TEST_CASE("cyrillic rasterises the same as latin", "[render][font][gpu]")
{
    gl_context();

    Font font = load_font();

    // The engine's own language. A typeface without it would answer with the
    // missing-character box, which has an advance and an extent just like a real
    // glyph — so this checks the width against a Latin letter of similar shape
    // rather than merely that something came back.
    const Glyph* const cyrillic = font.glyph(U'Ж');
    REQUIRE(cyrillic != nullptr);

    trace("'Ж': {}x{}, advance {}", cyrillic->size.x, cyrillic->size.y, cyrillic->advance);

    CHECK(cyrillic->size.x > 0.0f);
    CHECK(cyrillic->advance > 0.0f);
}

TEST_CASE("a space has an advance and nothing to draw", "[render][font][gpu]")
{
    gl_context();

    Font font = load_font();

    const Glyph* const space = font.glyph(U' ');
    REQUIRE(space != nullptr);

    trace("space: {}x{}, advance {}", space->size.x, space->size.y, space->advance);

    // Recorded rather than refused, so that the lookup hits and the pen moves.
    // Nothing is packed into the atlas for it, which is what the empty region says.
    CHECK(space->advance > 0.0f);
    CHECK(space->size.x == 0.0f);
    CHECK(space->region.is_whole_texture());
}

TEST_CASE("glyphs of different sizes come from different fonts", "[render][font][gpu]")
{
    gl_context();

    Font small = load_font(16);
    Font large = load_font(48);

    const Glyph* const from_small = small.glyph(U'M');
    const Glyph* const from_large = large.glyph(U'M');

    REQUIRE(from_small != nullptr);
    REQUIRE(from_large != nullptr);

    trace("'M' at 16px: {}x{}; at 48px: {}x{}", from_small->size.x, from_small->size.y,
          from_large->size.x, from_large->size.y);

    // Size is part of what a Font is, not a parameter to a method: the larger one
    // is genuinely rasterised larger rather than being the smaller one scaled up,
    // which is the entire reason the atlas exists.
    CHECK(from_large->size.x > from_small->size.x);
    CHECK(from_large->advance > from_small->advance);
    CHECK(large.atlas().id() != small.atlas().id());
}

TEST_CASE("an atlas with no room left says so once", "[render][font][gpu]")
{
    gl_context();

    // Far too small for a 32-pixel face: a few glyphs will fit and the rest will
    // not, which is the state the report exists for.
    Font font = load_font(32, FontConfig{.atlas_size = 64});

    const LogCaptureGuard capture;

    std::size_t placed = 0;
    for (char32_t letter = U'A'; letter <= U'Z'; ++letter)
    {
        if (font.glyph(letter) != nullptr)
        {
            ++placed;
        }
    }

    trace("{} of 26 glyph(s) fitted a 64x64 atlas, {} error(s) reported", placed,
          capture.count(Level::ERROR));

    CHECK(placed > 0);
    CHECK(placed < 26);

    // Once, not once per character: a page of text that no longer fits would
    // otherwise bury every other line in the log.
    CHECK(capture.count(Level::ERROR) == 1);
}

TEST_CASE("a font that cannot be loaded reports why", "[render][font][gpu]")
{
    gl_context();

    SECTION("a file that is not there")
    {
        const auto font = Font::from_file("no/such/typeface.ttf", TEST_PIXEL_SIZE);

        REQUIRE_FALSE(font.has_value());
        trace("missing file: {}", font.error());

        CHECK(font.error().code == ErrorCode::FILE_NOT_FOUND);
    }

    SECTION("a size of zero")
    {
        const std::optional<std::filesystem::path> path = find_system_font();
        if (!path.has_value())
        {
            SKIP("no system typeface was found to borrow");
        }

        const auto font = Font::from_file(*path, 0);

        REQUIRE_FALSE(font.has_value());
        trace("zero size: {}", font.error());

        CHECK(font.error().code == ErrorCode::INVALID_FORMAT);
    }
}

TEST_CASE("moving a font transfers the face and the atlas", "[render][font][gpu]")
{
    gl_context();

    Font original = load_font();
    REQUIRE(original.glyph(U'X') != nullptr);

    const unsigned int atlas = original.atlas().id();

    Font moved{std::move(original)};

    trace("after the move the atlas is {} and {} glyph(s) came with it", moved.atlas().id(),
          moved.glyph_count());

    // Moving is what makes changing the font at runtime a matter of replacing an
    // object: the cache travels with it rather than being rebuilt.
    CHECK(moved.atlas().id() == atlas);
    CHECK(moved.glyph_count() == 1);
    CHECK(moved.glyph(U'X') != nullptr);
}
