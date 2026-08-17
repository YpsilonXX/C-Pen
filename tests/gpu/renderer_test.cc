#include <catch2/catch_test_macros.hpp>

#include "cpen/render/pixel_format.hh"
#include "cpen/render/renderer.hh"
#include "cpen/render/sprite.hh"
#include "cpen/render/sprite_batch.hh"
#include "cpen/render/texture.hh"
#include "support/gl_fixture.hh"
#include "support/render_target.hh"
#include "support/trace.hh"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

using cpen::render::PixelFormat;
using cpen::render::Renderer;
using cpen::render::Sprite;
using cpen::render::SpriteBatch;
using cpen::render::Texture;
using cpen::test::gl_context;
using cpen::test::RenderTarget;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    constexpr int TARGET_SIZE = RenderTarget::SIZE;

    Renderer make_renderer()
    {
        auto renderer = Renderer::create(TARGET_SIZE, TARGET_SIZE);
        REQUIRE(renderer.has_value());
        return std::move(*renderer);
    }

    Texture white_texture()
    {
        constexpr std::array<std::byte, 4> WHITE = {
            std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};

        auto texture = Texture::from_pixels(WHITE, 1, 1, PixelFormat::RGBA8);
        REQUIRE(texture.has_value());
        return std::move(*texture);
    }

    void trace_pixel(const char* const label, const std::array<std::uint8_t, 4>& color)
    {
        trace("{}: r {}, g {}, b {}", label, static_cast<int>(color[0]),
              static_cast<int>(color[1]), static_cast<int>(color[2]));
    }
}

TEST_CASE("a created renderer owns what drawing needs", "[render][renderer][gpu]")
{
    gl_context();

    Renderer renderer = make_renderer();

    trace("can draw: {}, capacity {}", renderer.can_draw(),
          renderer.sprites()->capacity());

    CHECK(renderer.can_draw());
    REQUIRE(renderer.sprites() != nullptr);
    CHECK(renderer.sprites()->capacity() == SpriteBatch::DEFAULT_CAPACITY);
    CHECK_FALSE(renderer.is_frame_open());
}

TEST_CASE("beginning a frame clears and opens the batch", "[render][renderer][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    Renderer renderer = make_renderer();
    renderer.resize(TARGET_SIZE, TARGET_SIZE);

    constexpr glm::vec4 DEEP_BLUE{0.0f, 0.0f, 1.0f, 1.0f};
    renderer.set_clear_color(DEEP_BLUE);

    renderer.begin_frame();

    CHECK(renderer.is_frame_open());
    CHECK(renderer.sprites()->is_open());

    const auto centre = target.centre_pixel();
    trace_pixel("after begin_frame", centre);

    // The clear happens at begin_frame, so a state's render() never has to do it
    // and two states in a stack cannot clear over one another.
    CHECK(centre[2] == 255);
    CHECK(centre[0] == 0);

    renderer.end_frame();
    CHECK_FALSE(renderer.is_frame_open());
    CHECK_FALSE(renderer.sprites()->is_open());
}

TEST_CASE("the frame's batch is shared by everything drawn in it",
          "[render][renderer][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    Renderer renderer = make_renderer();
    renderer.resize(TARGET_SIZE, TARGET_SIZE);
    renderer.set_clear_color(glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});

    const Texture texture = white_texture();

    renderer.begin_frame();

    trace_step("two submissions standing in for two states of a stack");

    // What the Application does around the whole render pass, and the reason it is
    // done there: a state below and a state above put their sprites into one batch,
    // so an overlay sharing a texture with what it covers costs no extra draw call
    // at all.
    for (int index = 0; index < 2; ++index)
    {
        renderer.sprites()->draw(texture, Sprite{
                                              .position = {static_cast<float>(index) * 20.0f,
                                                           0.0f},
                                              .size = {16.0f, 16.0f},
                                              .color = {1.0f, 0.0f, 0.0f, 1.0f},
                                          });
    }

    renderer.end_frame();

    trace("{} sprite(s) in {} draw call(s)", renderer.sprite_count(),
          renderer.draw_calls());

    CHECK(renderer.sprite_count() == 2);
    CHECK(renderer.draw_calls() == 1);

    // Readable after the frame closed, which is what makes the tally usable for a
    // diagnostic drawn on the next one.
    const auto first = target.pixel_at(8, TARGET_SIZE - 9);
    trace_pixel("first sprite", first);
    CHECK(first[0] == 255);
}

TEST_CASE("resizing applies the letterboxed rectangle", "[render][renderer][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    Renderer renderer = make_renderer();

    // Wider than the virtual space's own ratio, so the content is centred with bars
    // at the sides and the rectangle GL is given is not the whole framebuffer.
    renderer.resize(TARGET_SIZE, TARGET_SIZE / 2);

    std::array<GLint, 4> applied{};
    glGetIntegerv(GL_VIEWPORT, applied.data());

    trace("viewport now {} {} {}x{}", applied[0], applied[1], applied[2], applied[3]);

    CHECK(applied[2] == renderer.viewport().rect().width);
    CHECK(applied[3] == renderer.viewport().rect().height);
    CHECK(applied[0] == renderer.viewport().rect().x);
    CHECK(applied[1] == renderer.viewport().rect().y);

    // Put back, since one context serves every case in this process.
    renderer.resize(TARGET_SIZE, TARGET_SIZE);
}

TEST_CASE("the clear covers the bars as well as the content", "[render][renderer][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    Renderer renderer = make_renderer();

    // A virtual space taller than it is wide, letterboxed into a square target:
    // there are bars down both sides that no sprite will ever cover.
    Renderer narrow = std::move(renderer);
    narrow.resize(TARGET_SIZE, TARGET_SIZE);

    narrow.set_clear_color(glm::vec4{0.0f, 1.0f, 0.0f, 1.0f});
    narrow.begin_frame();
    narrow.end_frame();

    const auto corner = target.pixel_at(0, 0);
    const auto centre = target.centre_pixel();

    trace_pixel("corner", corner);
    trace_pixel("centre", centre);

    // glClear ignores the viewport rectangle, which is what is wanted here:
    // scissoring it to the content would leave the bars holding whatever the
    // previous frame put there.
    CHECK(corner[1] == 255);
    CHECK(centre[1] == 255);
}
