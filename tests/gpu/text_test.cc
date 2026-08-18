#include <catch2/catch_test_macros.hpp>

#include "cpen/render/font.hh"
#include "cpen/render/pixel_format.hh"
#include "cpen/render/sprite_batch.hh"
#include "cpen/render/text.hh"
#include "cpen/render/texture.hh"
#include "cpen/render/viewport.hh"
#include "support/gl_fixture.hh"
#include "support/render_target.hh"
#include "support/engine_font.hh"
#include "support/trace.hh"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

using cpen::render::Font;
using cpen::render::PixelFormat;
using cpen::render::Sprite;
using cpen::render::SpriteBatch;
using cpen::render::Texture;
using cpen::render::TextureConfig;
using cpen::render::Viewport;
using cpen::render::draw_text;
using cpen::render::measure_text;
using cpen::render::wrap_text;
using cpen::test::engine_font_path;
using cpen::test::gl_context;
using cpen::test::RenderTarget;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    /// Room for a couple of lines at a readable size. The default 64 is smaller
    /// than one line of the 24-pixel face these cases use.
    constexpr int TEXT_TARGET_SIZE = 160;

    constexpr std::uint32_t TEXT_PIXEL_SIZE = 24;

    constexpr glm::vec4 BLACK{0.0f, 0.0f, 0.0f, 1.0f};

    Font load_font(const std::uint32_t pixel_size = TEXT_PIXEL_SIZE)
    {
        auto font = Font::from_file(engine_font_path(), pixel_size);
        REQUIRE(font.has_value());
        return std::move(*font);
    }

    SpriteBatch make_batch()
    {
        auto batch = SpriteBatch::create();
        REQUIRE(batch.has_value());
        return std::move(*batch);
    }

    Viewport target_viewport(const RenderTarget& target)
    {
        const auto side = static_cast<std::uint32_t>(target.size());
        Viewport viewport{side, side};
        viewport.resize(side, side);
        return viewport;
    }

    /// The whole framebuffer in one read.
    ///
    /// One glReadPixels rather than one per pixel: a case here looks at every
    /// pixel of the target, and twenty-five thousand round trips to the driver
    /// would make the suite noticeably slower for no more information.
    std::vector<std::uint8_t> read_all(const RenderTarget& target)
    {
        const auto side = target.size();
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(side) *
                                         static_cast<std::size_t>(side) * 4);

        glReadPixels(0, 0, side, side, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    }

    struct Coverage
    {
        std::size_t lit = 0;
        std::size_t red = 0;
        std::size_t green = 0;
    };

    /// Counts pixels that were written to, and what colour they came out.
    ///
    /// Counting rather than sampling a chosen point: which pixels a glyph covers is
    /// the typeface's business, and a case that named one would be asserting on the
    /// shape of a letter in a font it borrowed from the machine.
    Coverage measure_coverage(const RenderTarget& target)
    {
        const std::vector<std::uint8_t> pixels = read_all(target);

        Coverage coverage;
        for (std::size_t index = 0; index + 3 < pixels.size(); index += 4)
        {
            const std::uint8_t red = pixels[index];
            const std::uint8_t green = pixels[index + 1];
            const std::uint8_t blue = pixels[index + 2];

            if (red == 0 && green == 0 && blue == 0)
            {
                continue;
            }

            ++coverage.lit;

            if (red > green && red > blue)
            {
                ++coverage.red;
            }
            if (green > red && green > blue)
            {
                ++coverage.green;
            }
        }

        return coverage;
    }

    Texture white_texture()
    {
        constexpr std::array<std::byte, 4> WHITE = {
            std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};

        auto texture = Texture::from_pixels(WHITE, 1, 1, PixelFormat::RGBA8);
        REQUIRE(texture.has_value());
        return std::move(*texture);
    }
}

TEST_CASE("drawing text puts glyphs on the framebuffer in the tint's colour",
          "[render][text][gpu]")
{
    gl_context();

    const RenderTarget target{TEXT_TARGET_SIZE};
    REQUIRE(target.is_complete());

    Font font = load_font();
    SpriteBatch batch = make_batch();
    const Viewport viewport = target_viewport(target);

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());
    draw_text(batch, font, "Hi", glm::vec2{8.0f, 8.0f}, glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});
    batch.end();

    const Coverage coverage = measure_coverage(target);
    trace("{} lit pixel(s), {} of them red", coverage.lit, coverage.red);

    CHECK(coverage.lit > 0);

    // The whole point of the alpha-only path. A one-channel atlas samples as
    // (coverage, 0, 0, 1); multiplied by the tint the way an RGBA texture is, every
    // glyph would come out red whatever colour was asked for — so a red tint would
    // pass this by accident. It is asserted to be red *and* the next case asks for
    // green, which the naive path could not produce at all.
    CHECK(coverage.red == coverage.lit);

    SECTION("and a green tint really comes out green")
    {
        cpen::render::clear(BLACK);

        batch.begin(viewport.projection());
        draw_text(batch, font, "Hi", glm::vec2{8.0f, 8.0f},
                  glm::vec4{0.0f, 1.0f, 0.0f, 1.0f});
        batch.end();

        const Coverage green_coverage = measure_coverage(target);
        trace("{} lit pixel(s), {} of them green", green_coverage.lit,
              green_coverage.green);

        CHECK(green_coverage.lit > 0);
        CHECK(green_coverage.green == green_coverage.lit);
    }
}

TEST_CASE("a line of text costs one draw call", "[render][text][gpu]")
{
    gl_context();

    const RenderTarget target{TEXT_TARGET_SIZE};
    REQUIRE(target.is_complete());

    Font font = load_font();
    SpriteBatch batch = make_batch();
    const Viewport viewport = target_viewport(target);

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());
    draw_text(batch, font, "Привет", glm::vec2{4.0f, 4.0f});
    batch.end();

    trace("{} glyph(s) in {} draw call(s)", batch.sprite_count(), batch.draw_calls());

    // Every glyph shares one atlas, so a line is one run however long it is.
    CHECK(batch.draw_calls() == 1);
    CHECK(batch.sprite_count() == 6);

    SECTION("and spaces cost no sprite at all")
    {
        batch.begin(viewport.projection());
        draw_text(batch, font, "a b", glm::vec2{4.0f, 4.0f});
        batch.end();

        trace("'a b' submitted {} sprite(s)", batch.sprite_count());

        // Three characters, two of them with ink. Submitting the space would spend
        // an instance record on an empty quad.
        CHECK(batch.sprite_count() == 2);
    }
}

TEST_CASE("switching font costs exactly what switching texture costs",
          "[render][text][gpu]")
{
    gl_context();

    const RenderTarget target{TEXT_TARGET_SIZE};
    REQUIRE(target.is_complete());

    Font small = load_font(16);
    Font large = load_font(32);
    SpriteBatch batch = make_batch();
    const Viewport viewport = target_viewport(target);

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());
    draw_text(batch, small, "aa", glm::vec2{4.0f, 4.0f});
    draw_text(batch, large, "bb", glm::vec2{4.0f, 60.0f});
    draw_text(batch, small, "cc", glm::vec2{4.0f, 110.0f});
    batch.end();

    trace("three runs across two fonts: {} sprite(s) in {} draw call(s)",
          batch.sprite_count(), batch.draw_calls());

    // Each font owns its atlas, so alternating between them splits the batch just
    // as alternating between two pictures would. Nothing about changing font is
    // special, which is the property that makes it cheap to do at runtime.
    CHECK(batch.sprite_count() == 6);
    CHECK(batch.draw_calls() == 3);

    const Coverage coverage = measure_coverage(target);
    trace("{} lit pixel(s) across all three runs", coverage.lit);
    CHECK(coverage.lit > 0);
}

TEST_CASE("text and pictures share one batch", "[render][text][gpu]")
{
    gl_context();

    const RenderTarget target{TEXT_TARGET_SIZE};
    REQUIRE(target.is_complete());

    Font font = load_font();
    SpriteBatch batch = make_batch();
    const Texture picture = white_texture();
    const Viewport viewport = target_viewport(target);

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());

    // A four-channel picture and a one-channel atlas in the same batch. The
    // alpha-only flag is a uniform written per flush rather than per batch
    // precisely so that these two can follow one another: written once at begin()
    // it would be wrong for one of them, and the picture would come out as a red
    // block or the text as red boxes.
    batch.draw(picture, Sprite{
                            .position = {0.0f, 0.0f},
                            .size = {TEXT_TARGET_SIZE / 2.0f, TEXT_TARGET_SIZE},
                            .color = {1.0f, 0.0f, 0.0f, 1.0f},
                        });

    draw_text(batch, font, "Hi", glm::vec2{TEXT_TARGET_SIZE / 2.0f + 8.0f, 8.0f},
              glm::vec4{0.0f, 1.0f, 0.0f, 1.0f});

    batch.end();

    trace("{} sprite(s) in {} draw call(s)", batch.sprite_count(), batch.draw_calls());
    CHECK(batch.draw_calls() == 2);

    const Coverage coverage = measure_coverage(target);
    trace("{} lit: {} red from the picture, {} green from the text", coverage.lit,
          coverage.red, coverage.green);

    // Both came out in their own colour, which neither would have done if the flag
    // had been decided once for the whole batch.
    CHECK(coverage.red > 0);
    CHECK(coverage.green > 0);
}

TEST_CASE("measuring text reports a box in whole lines", "[render][text][gpu]")
{
    gl_context();

    Font font = load_font();

    const glm::vec2 empty = measure_text(font, "");
    const glm::vec2 one = measure_text(font, "a");
    const glm::vec2 two = measure_text(font, "aa");
    const glm::vec2 stacked = measure_text(font, "a\na");

    trace("empty: {}x{}", empty.x, empty.y);
    trace("'a': {}x{}, 'aa': {}x{}", one.x, one.y, two.x, two.y);
    trace("'a\\na': {}x{}", stacked.x, stacked.y);

    // An empty label still occupies its row, so a column of labels does not
    // collapse where one of them happens to have nothing in it.
    CHECK(empty.x == 0.0f);
    CHECK(empty.y == font.line_height());

    CHECK(two.x > one.x);
    CHECK(one.y == font.line_height());

    // Two lines are exactly two line heights, and the width is the wider line's
    // rather than the total.
    CHECK(stacked.y == 2.0f * font.line_height());
    CHECK(stacked.x == one.x);
}

TEST_CASE("wrapping with a real font keeps every line inside the width",
          "[render][text][gpu]")
{
    gl_context();

    Font font = load_font();

    constexpr std::string_view TEXT =
        "Съешь же ещё этих мягких французских булок да выпей чаю";
    constexpr float WIDTH = 300.0f;

    const std::vector<std::string_view> lines = wrap_text(font, TEXT, WIDTH);

    REQUIRE(lines.size() > 1);

    for (const std::string_view line : lines)
    {
        const glm::vec2 measured = measure_text(font, line);
        trace("{:>6.1f} | {}", measured.x, line);

        // The property the wrapper exists for, checked against the same metrics it
        // wrapped by rather than against a count of characters.
        CHECK(measured.x <= WIDTH);
    }
}
