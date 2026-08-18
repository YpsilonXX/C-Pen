#include <catch2/catch_test_macros.hpp>

#include "cpen/render/draw.hh"
#include "cpen/render/image.hh"
#include "cpen/render/pixel_format.hh"
#include "cpen/render/sprite_batch.hh"
#include "cpen/render/texture.hh"
#include "cpen/render/viewport.hh"
#include "support/gl_fixture.hh"
#include "support/log_capture.hh"
#include "support/render_target.hh"
#include "support/trace.hh"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

using cpen::log::Level;
using cpen::render::PixelFormat;
using cpen::render::Sprite;
using cpen::render::SpriteBatch;
using cpen::render::Texture;
using cpen::render::TextureConfig;
using cpen::render::TextureFilter;
using cpen::render::TextureRegion;
using cpen::render::Viewport;
using cpen::test::gl_context;
using cpen::test::LogCaptureGuard;
using cpen::test::RenderTarget;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    constexpr int TARGET_SIZE = RenderTarget::SIZE;

    constexpr glm::vec4 BLACK{0.0f, 0.0f, 0.0f, 1.0f};

    /// A viewport whose virtual space is exactly the target, so that one virtual
    /// pixel is one pixel read back and a coordinate in a case means what it says.
    Viewport target_viewport()
    {
        Viewport viewport{TARGET_SIZE, TARGET_SIZE};
        viewport.resize(TARGET_SIZE, TARGET_SIZE);
        return viewport;
    }

    /// Builds a texture from RGBA bytes, unfiltered so that a texel is sampled as
    /// itself and a region test cannot be passed by a blend of two neighbours.
    Texture make_texture(const std::initializer_list<std::uint8_t> channels,
                         const std::uint32_t width, const std::uint32_t height)
    {
        std::vector<std::byte> pixels;
        pixels.reserve(channels.size());
        for (const std::uint8_t channel : channels)
        {
            pixels.push_back(static_cast<std::byte>(channel));
        }

        auto texture = Texture::from_pixels(pixels, width, height, PixelFormat::RGBA8,
                                            TextureConfig{
                                                .minify_filter = TextureFilter::NEAREST,
                                                .magnify_filter = TextureFilter::NEAREST,
                                            });
        REQUIRE(texture.has_value());
        return std::move(*texture);
    }

    Texture white_texture()
    {
        return make_texture({255, 255, 255, 255}, 1, 1);
    }

    SpriteBatch make_batch(const std::size_t capacity = SpriteBatch::DEFAULT_CAPACITY)
    {
        auto batch = SpriteBatch::create(capacity);
        REQUIRE(batch.has_value());
        return std::move(*batch);
    }

    /// glReadPixels counts rows from the bottom; virtual space counts them from the
    /// top. Converting here, once and by name, keeps every case below written in
    /// the coordinates the sprites were submitted in.
    std::array<std::uint8_t, 4> pixel_at_virtual(const RenderTarget& target, const int x,
                                                 const int y)
    {
        return target.pixel_at(x, target.size() - 1 - y);
    }

    void trace_pixel(const char* const label, const std::array<std::uint8_t, 4>& color)
    {
        trace("{}: r {}, g {}, b {}, a {}", label, static_cast<int>(color[0]),
              static_cast<int>(color[1]), static_cast<int>(color[2]),
              static_cast<int>(color[3]));
    }
}

TEST_CASE("a sprite is drawn where its virtual coordinates put it",
          "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Viewport viewport = target_viewport();
    const Texture texture = white_texture();
    SpriteBatch batch = make_batch();

    // The top-left quadrant of virtual space. Asserting on the *bottom* left as
    // well is what makes this a test of the y direction rather than only of the
    // draw: the two disagree, and a projection flipped the wrong way swaps them.
    const Sprite sprite{
        .position = {0.0f, 0.0f},
        .size = {TARGET_SIZE / 2.0f, TARGET_SIZE / 2.0f},
        .color = {1.0f, 0.0f, 0.0f, 1.0f},
    };

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());
    batch.draw(texture, sprite);
    batch.end();

    const auto top_left = pixel_at_virtual(target, TARGET_SIZE / 4, TARGET_SIZE / 4);
    const auto bottom_left = pixel_at_virtual(target, TARGET_SIZE / 4, 3 * TARGET_SIZE / 4);

    trace_pixel("virtual top left", top_left);
    trace_pixel("virtual bottom left", bottom_left);
    trace("{} sprite(s) in {} draw call(s)", batch.sprite_count(), batch.draw_calls());

    CHECK(top_left[0] == 255);
    CHECK(bottom_left[0] == 0);

    CHECK(batch.sprite_count() == 1);
    CHECK(batch.draw_calls() == 1);
    CHECK_FALSE(batch.is_open());
}

TEST_CASE("sprites sharing a texture cost one draw call", "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Viewport viewport = target_viewport();
    const Texture first = white_texture();
    const Texture second = white_texture();
    SpriteBatch batch = make_batch();

    const Sprite sprite{.size = {8.0f, 8.0f}, .color = {1.0f, 1.0f, 1.0f, 1.0f}};

    SECTION("a run of one texture is a single call")
    {
        batch.begin(viewport.projection());
        batch.draw(first, sprite);
        batch.draw(first, sprite);
        batch.draw(first, sprite);
        batch.end();

        trace("{} sprite(s) in {} draw call(s)", batch.sprite_count(), batch.draw_calls());

        CHECK(batch.sprite_count() == 3);
        CHECK(batch.draw_calls() == 1);
    }

    SECTION("and every change of texture ends the run")
    {
        // Deliberately alternating rather than grouped. The batch does not reorder
        // to make the runs longer, because submission order is what decides which
        // sprite is on top, and three calls is the honest price of asking for this.
        batch.begin(viewport.projection());
        batch.draw(first, sprite);
        batch.draw(second, sprite);
        batch.draw(first, sprite);
        batch.end();

        trace("{} sprite(s) in {} draw call(s)", batch.sprite_count(), batch.draw_calls());

        CHECK(batch.sprite_count() == 3);
        CHECK(batch.draw_calls() == 3);
    }
}

TEST_CASE("a full batch flushes and keeps collecting", "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Viewport viewport = target_viewport();
    const Texture texture = white_texture();
    SpriteBatch batch = make_batch(2);

    REQUIRE(batch.capacity() == 2);

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());

    // Five sprites through a batch that holds two: two full flushes and the
    // remainder at end(). Each one is somewhere different, so a sprite lost at a
    // flush boundary leaves a hole that the pixel checks below would find.
    for (int index = 0; index < 5; ++index)
    {
        batch.draw(texture, Sprite{
                                .position = {static_cast<float>(index * 12), 0.0f},
                                .size = {8.0f, 8.0f},
                                .color = {1.0f, 0.0f, 0.0f, 1.0f},
                            });
    }

    batch.end();

    trace("{} sprite(s) in {} draw call(s)", batch.sprite_count(), batch.draw_calls());

    CHECK(batch.sprite_count() == 5);
    CHECK(batch.draw_calls() == 3);

    for (int index = 0; index < 5; ++index)
    {
        const auto pixel = pixel_at_virtual(target, index * 12 + 4, 4);
        trace_pixel("sprite", pixel);
        CHECK(pixel[0] == 255);
    }
}

TEST_CASE("the tint multiplies the sampled colour", "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Viewport viewport = target_viewport();
    const Texture texture = white_texture();
    SpriteBatch batch = make_batch();

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());
    batch.draw(texture, Sprite{
                            .position = {0.0f, 0.0f},
                            .size = {TARGET_SIZE, TARGET_SIZE},
                            .color = {1.0f, 0.5f, 0.0f, 1.0f},
                        });
    batch.end();

    const auto centre = target.centre_pixel();
    trace_pixel("tinted white texture", centre);

    // The tint travels as four bytes and comes back normalised, so half of full
    // intensity is 128 rather than 127: the packing rounds instead of truncating,
    // which is what keeps a tint of 1.0 from arriving as 254.
    CHECK(centre[0] == 255);
    CHECK(centre[1] >= 127);
    CHECK(centre[1] <= 129);
    CHECK(centre[2] == 0);
}

TEST_CASE("a region selects part of the texture", "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Viewport viewport = target_viewport();

    // Two texels side by side: red on the left, green on the right. A region that
    // was ignored, or normalised against the wrong axis, samples the red one.
    const Texture texture = make_texture({255, 0, 0, 255, 0, 255, 0, 255}, 2, 1);

    SpriteBatch batch = make_batch();

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());
    batch.draw(texture, Sprite{
                            .position = {0.0f, 0.0f},
                            .size = {TARGET_SIZE, TARGET_SIZE},
                            .region = TextureRegion{.position = {1.0f, 0.0f},
                                                    .size = {1.0f, 1.0f}},
                        });
    batch.end();

    const auto centre = target.centre_pixel();
    trace_pixel("right-hand texel", centre);

    CHECK(centre[1] == 255);
    CHECK(centre[0] == 0);

    SECTION("and no region at all takes the whole texture")
    {
        cpen::render::clear(BLACK);

        batch.begin(viewport.projection());
        batch.draw(texture, Sprite{
                                .position = {0.0f, 0.0f},
                                .size = {TARGET_SIZE, TARGET_SIZE},
                            });
        batch.end();

        // Stretched across the target, the two texels each take a half, so the two
        // sides differ where the region case had them the same.
        const auto left = pixel_at_virtual(target, TARGET_SIZE / 4, TARGET_SIZE / 2);
        const auto right = pixel_at_virtual(target, 3 * TARGET_SIZE / 4, TARGET_SIZE / 2);

        trace_pixel("left half", left);
        trace_pixel("right half", right);

        CHECK(left[0] == 255);
        CHECK(right[1] == 255);
    }
}

TEST_CASE("a sprite with alpha is blended into what is there",
          "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Viewport viewport = target_viewport();
    const Texture texture = white_texture();
    SpriteBatch batch = make_batch();

    // Full red underneath, half-transparent green over it. Without blending the
    // result is pure green and the red channel reads zero.
    cpen::render::clear(glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});

    batch.begin(viewport.projection());
    batch.draw(texture, Sprite{
                            .position = {0.0f, 0.0f},
                            .size = {TARGET_SIZE, TARGET_SIZE},
                            .color = {0.0f, 1.0f, 0.0f, 0.5f},
                        });
    batch.end();

    const auto centre = target.centre_pixel();
    trace_pixel("half-transparent green over red", centre);

    CHECK(centre[0] > 100);
    CHECK(centre[0] < 155);
    CHECK(centre[1] > 100);
    CHECK(centre[1] < 155);

    SECTION("and blending is off again once the batch has closed")
    {
        GLboolean enabled = GL_TRUE;
        glGetBooleanv(GL_BLEND, &enabled);

        trace("blending after end(): {}", enabled == GL_TRUE);

        // The batch puts back what it found. One context serves every case in this
        // process, so state left enabled here would surface as an unrelated failure
        // somewhere else entirely.
        CHECK(enabled == GL_FALSE);
    }
}

TEST_CASE("a premultiplied texture composites as the artwork intended",
          "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Viewport viewport = target_viewport();
    SpriteBatch batch = make_batch();

    // White at half alpha, put through the same step the asset layer applies to
    // every picture it loads. What reaches the driver is (128, 128, 128, 128).
    std::vector<std::byte> pixels;
    for (const std::uint8_t value : std::array<std::uint8_t, 4>{255, 255, 255, 128})
    {
        pixels.push_back(static_cast<std::byte>(value));
    }

    auto image = cpen::render::Image::from_pixels(std::move(pixels), 1, 1, PixelFormat::RGBA8);
    REQUIRE(image.has_value());
    image->premultiply_alpha();

    auto texture = Texture::from_image(*image, TextureConfig{
                                                   .minify_filter = TextureFilter::NEAREST,
                                                   .magnify_filter = TextureFilter::NEAREST,
                                               });
    REQUIRE(texture.has_value());

    cpen::render::clear(glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});

    batch.begin(viewport.projection());
    batch.draw(*texture, Sprite{
                             .position = {0.0f, 0.0f},
                             .size = {TARGET_SIZE, TARGET_SIZE},
                         });
    batch.end();

    const auto centre = target.centre_pixel();
    trace_pixel("half-transparent white over red", centre);

    // Half white over full red is (128, 128, 128) + (127, 0, 0) — the same answer
    // straight alpha would give, which is the point: premultiplying the pixels and
    // blending with GL_ONE is not a different look, it is the same composite done
    // where filtering cannot spoil it.
    CHECK(centre[0] > 235);
    CHECK(centre[1] > 100);
    CHECK(centre[1] < 155);
    CHECK(centre[2] > 100);
    CHECK(centre[2] < 155);

    SECTION("and the same pixels drawn straight would be visibly darker")
    {
        // Not a second draw: the arithmetic is the whole argument. Straight
        // blending would scale the already-multiplied 128 by its own alpha again,
        // giving 64 where the correct answer is 128 — the doubled darkening that
        // makes the mode a property of the pixels rather than a preference.
        constexpr int PREMULTIPLIED_RESULT = 128;
        constexpr int STRAIGHT_RESULT = (128 * 128 + 127) / 255;

        trace("premultiplied {}, straight would be {}", PREMULTIPLIED_RESULT,
              STRAIGHT_RESULT);

        CHECK(STRAIGHT_RESULT < PREMULTIPLIED_RESULT);
        CHECK(centre[1] > STRAIGHT_RESULT + 20);
    }
}

TEST_CASE("drawing outside a batch is reported once and drops the sprite",
          "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Texture texture = white_texture();
    SpriteBatch batch = make_batch();

    const LogCaptureGuard capture;

    batch.draw(texture, Sprite{.size = {8.0f, 8.0f}});
    batch.draw(texture, Sprite{.size = {8.0f, 8.0f}});
    batch.draw(texture, Sprite{.size = {8.0f, 8.0f}});

    trace("three sprites outside a batch produced {} error(s)", capture.count(Level::ERROR));

    // Once, not three times. A draw call made from the wrong place is made every
    // frame, and sixty identical lines a second bury the one that mattered.
    CHECK(capture.count(Level::ERROR) == 1);
    CHECK(batch.sprite_count() == 0);
}

TEST_CASE("reopening a batch draws what the previous one had collected",
          "[render][sprite_batch][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    const Viewport viewport = target_viewport();
    const Texture texture = white_texture();
    SpriteBatch batch = make_batch();

    cpen::render::clear(BLACK);

    batch.begin(viewport.projection());
    batch.draw(texture, Sprite{
                            .position = {0.0f, 0.0f},
                            .size = {16.0f, 16.0f},
                            .color = {1.0f, 0.0f, 0.0f, 1.0f},
                        });

    const LogCaptureGuard capture;
    batch.begin(viewport.projection());

    trace_step("the sprite from the abandoned batch reaches the framebuffer anyway");

    batch.end();

    const auto pixel = pixel_at_virtual(target, 8, 8);
    trace_pixel("sprite from before the second begin()", pixel);

    CHECK(capture.count(Level::ERROR) == 1);
    CHECK(pixel[0] == 255);

    // The counters belong to the batch that is open, so the sprite drawn above is
    // not one of this batch's.
    CHECK(batch.sprite_count() == 0);
}
