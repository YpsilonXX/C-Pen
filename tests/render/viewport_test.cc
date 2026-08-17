#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "cpen/render/viewport.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <glm/glm.hpp>

using cpen::log::Level;
using cpen::render::ScaleMode;
using cpen::render::Viewport;
using cpen::render::ViewportRect;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;
using cpen::test::trace_step;
using Catch::Matchers::WithinAbs;

// Viewport is pure arithmetic over the framebuffer size — it makes no GL call and
// needs no context — so its tests belong in the suite that runs on a machine with
// no driver and no display, next to the image and vertex-layout cases.

namespace
{
    /// Tolerance for a coordinate that has been through a divide and a multiply.
    /// Generous by float standards and still far below the pixel these values are
    /// eventually rounded to.
    constexpr float TOLERANCE = 0.001f;

    void require_point(const glm::vec2 actual, const float x, const float y)
    {
        REQUIRE_THAT(actual.x, WithinAbs(x, TOLERANCE));
        REQUIRE_THAT(actual.y, WithinAbs(y, TOLERANCE));
    }

    void trace_rect(const ViewportRect& rect)
    {
        trace("rect: x={} y={} {}x{}", rect.x, rect.y, rect.width, rect.height);
    }
}

TEST_CASE("virtual space is projected with its origin at the top left", "[render][viewport]")
{
    const Viewport viewport{1920, 1080};

    const glm::mat4 projection = viewport.projection();

    // The corners carried into clip space by hand. With the origin at the top left
    // and y growing downwards, virtual (0, 0) must arrive at the top left of the
    // clip volume, which is NDC (-1, +1) — the +1 is the whole point.
    const glm::vec4 top_left = projection * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec4 bottom_right = projection * glm::vec4{1920.0f, 1080.0f, 0.0f, 1.0f};
    const glm::vec4 centre = projection * glm::vec4{960.0f, 540.0f, 0.0f, 1.0f};

    trace("virtual (0, 0) -> ndc ({}, {})", top_left.x, top_left.y);
    trace("virtual (1920, 1080) -> ndc ({}, {})", bottom_right.x, bottom_right.y);

    require_point({top_left.x, top_left.y}, -1.0f, 1.0f);
    require_point({bottom_right.x, bottom_right.y}, 1.0f, -1.0f);
    require_point({centre.x, centre.y}, 0.0f, 0.0f);
}

TEST_CASE("a framebuffer of the design aspect ratio gets no bars", "[render][viewport]")
{
    Viewport viewport{1920, 1080};

    SECTION("at exactly the virtual size")
    {
        viewport.resize(1920, 1080);
        trace_rect(viewport.rect());

        REQUIRE(viewport.rect() == ViewportRect{0, 0, 1920, 1080});
        require_point(viewport.scale(), 1.0f, 1.0f);
    }

    SECTION("at half of it")
    {
        viewport.resize(960, 540);
        trace_rect(viewport.rect());

        REQUIRE(viewport.rect() == ViewportRect{0, 0, 960, 540});
        require_point(viewport.scale(), 0.5f, 0.5f);
    }

    SECTION("at a larger framebuffer of the same shape")
    {
        viewport.resize(2560, 1440);
        trace_rect(viewport.rect());

        REQUIRE(viewport.rect() == ViewportRect{0, 0, 2560, 1440});
        require_point(viewport.scale(), 4.0f / 3.0f, 4.0f / 3.0f);
    }
}

TEST_CASE("a framebuffer wider than the design ratio gets bars at the sides",
          "[render][viewport]")
{
    Viewport viewport{1920, 1080};

    // 2560x1080, an ultrawide. Height is the limiting axis, so the content keeps
    // the full height and is centred horizontally.
    viewport.resize(2560, 1080);
    trace_rect(viewport.rect());

    REQUIRE(viewport.rect().height == 1080);
    REQUIRE(viewport.rect().width == 1920);
    REQUIRE(viewport.rect().y == 0);
    REQUIRE(viewport.rect().x == 320);

    require_point(viewport.scale(), 1.0f, 1.0f);

    trace_step("a point on a bar converts to a coordinate outside the virtual space");

    // Reported rather than clamped: a click ten pixels from the left edge of the
    // window is not a click on whatever happens to be drawn at virtual x = 0, and a
    // caller that wants it treated as a miss needs to be able to tell.
    require_point(viewport.to_virtual({10.0f, 540.0f}), -310.0f, 540.0f);
}

TEST_CASE("a framebuffer taller than the design ratio gets bars above and below",
          "[render][viewport]")
{
    Viewport viewport{1920, 1080};

    // 1600x1200, a 4:3 display. Width is the limiting axis this time.
    viewport.resize(1600, 1200);
    trace_rect(viewport.rect());

    const float factor = 1600.0f / 1920.0f;
    const auto expected_height = static_cast<int>(1080.0f * factor);

    REQUIRE(viewport.rect().width == 1600);
    REQUIRE(viewport.rect().x == 0);
    REQUIRE(viewport.rect().height == expected_height);
    REQUIRE(viewport.rect().y == (1200 - expected_height) / 2);
}

TEST_CASE("stretching fills the framebuffer and scales the axes apart", "[render][viewport]")
{
    Viewport viewport{1920, 1080, ScaleMode::STRETCH};

    viewport.resize(1920, 2160);
    trace_rect(viewport.rect());

    REQUIRE(viewport.rect() == ViewportRect{0, 0, 1920, 2160});
    require_point(viewport.scale(), 1.0f, 2.0f);

    trace_step("every virtual point still lands inside the framebuffer");

    require_point(viewport.to_virtual({0.0f, 0.0f}), 0.0f, 0.0f);
    require_point(viewport.to_virtual({1920.0f, 2160.0f}), 1920.0f, 1080.0f);
}

TEST_CASE("the limiting axis is filled from edge to edge", "[render][viewport]")
{
    Viewport viewport{1920, 1080};

    // The axis the scale factor was derived from must come back out as exactly the
    // framebuffer's size. It is a rounding case, not a scaling one: 1920 * (1000 /
    // 1920) evaluates to 999.99994 in single precision, and truncating that leaves
    // a black column one pixel wide down a side the letterbox has no business
    // putting a bar on at all.
    SECTION("when width is the limit")
    {
        viewport.resize(1000, 2000);
        trace_rect(viewport.rect());

        REQUIRE(viewport.rect().width == 1000);
        REQUIRE(viewport.rect().x == 0);
    }

    SECTION("when height is the limit")
    {
        viewport.resize(3000, 900);
        trace_rect(viewport.rect());

        REQUIRE(viewport.rect().height == 900);
        REQUIRE(viewport.rect().y == 0);
    }
}

TEST_CASE("converting between framebuffer and virtual coordinates round-trips",
          "[render][viewport]")
{
    // 1280x1024 is 5:4 — neither the design ratio nor a whole-number scale of it,
    // so the conversion is doing real work in both directions.
    const auto round_trip = [](const Viewport& viewport)
    {
        trace_rect(viewport.rect());

        for (const glm::vec2 point : {glm::vec2{0.0f, 0.0f}, glm::vec2{960.0f, 540.0f},
                                      glm::vec2{1920.0f, 1080.0f}, glm::vec2{123.0f, 456.0f}})
        {
            const glm::vec2 returned = viewport.to_virtual(viewport.to_framebuffer(point));
            trace("({}, {}) -> ({}, {})", point.x, point.y, returned.x, returned.y);
            require_point(returned, point.x, point.y);
        }
    };

    SECTION("letterboxed")
    {
        Viewport viewport{1920, 1080, ScaleMode::LETTERBOX};
        viewport.resize(1280, 1024);
        round_trip(viewport);
    }

    SECTION("stretched")
    {
        Viewport viewport{1920, 1080, ScaleMode::STRETCH};
        viewport.resize(1280, 1024);
        round_trip(viewport);
    }
}

TEST_CASE("virtual zero sits against the top bar, not the bottom one", "[render][viewport]")
{
    Viewport viewport{1920, 1080};

    // A framebuffer chosen so that the leftover space is an odd number of pixels:
    // 1000 wide scales the content to 563 tall, leaving 1000 - 563 = 437, which
    // cannot be split evenly. The two bars then differ by exactly one pixel, and
    // reading the rectangle's y — measured from the *bottom* — where the top margin
    // is meant would be off by that pixel.
    viewport.resize(1000, 1000);
    trace_rect(viewport.rect());

    const auto top_margin =
        static_cast<float>(1000 - viewport.rect().height - viewport.rect().y);
    trace("bottom margin {}, top margin {}", viewport.rect().y, top_margin);

    // The premise of the case. If this ever stops holding, the case is testing
    // nothing and the size above needs choosing again.
    REQUIRE(viewport.rect().y != static_cast<int>(top_margin));

    trace_step("the corner of the content is virtual (0, 0), measured from the top");

    require_point(viewport.to_virtual({static_cast<float>(viewport.rect().x), top_margin}),
                  0.0f, 0.0f);
    require_point(viewport.to_framebuffer({0.0f, 0.0f}),
                  static_cast<float>(viewport.rect().x), top_margin);
}

TEST_CASE("a minimised window yields an empty rectangle rather than a division by zero",
          "[render][viewport]")
{
    Viewport viewport{1920, 1080};
    viewport.resize(1920, 1080);

    viewport.resize(0, 0);
    trace_rect(viewport.rect());

    REQUIRE(viewport.rect() == ViewportRect{0, 0, 0, 0});
    require_point(viewport.scale(), 0.0f, 0.0f);

    // The value the conversions answer with is arbitrary; that it is finite is not.
    // A NaN here would spread through every position derived from it and surface
    // only as geometry that silently never appears.
    const glm::vec2 converted = viewport.to_virtual({100.0f, 100.0f});
    trace("a point converted while minimised: ({}, {})", converted.x, converted.y);

    REQUIRE(converted.x == converted.x);
    REQUIRE(converted.y == converted.y);

    SECTION("and the mapping returns when the window is restored")
    {
        viewport.resize(1920, 1080);

        REQUIRE(viewport.rect() == ViewportRect{0, 0, 1920, 1080});
        require_point(viewport.scale(), 1.0f, 1.0f);
    }
}

TEST_CASE("a zero virtual dimension is reported and replaced", "[render][viewport]")
{
    const LogCaptureGuard capture;

    const Viewport viewport{0, 1080};

    REQUIRE(capture.count(Level::ERROR) == 1);
    REQUIRE(viewport.virtual_size().x == 1);
    REQUIRE(viewport.virtual_size().y == 1080);
}

TEST_CASE("the projection does not change when the framebuffer does", "[render][viewport]")
{
    Viewport viewport{1920, 1080};
    viewport.resize(1920, 1080);

    const glm::mat4 before = viewport.projection();

    viewport.resize(640, 2000);
    trace_rect(viewport.rect());

    // Letterboxing moves the picture within the framebuffer; it never alters what
    // the virtual space is. This is why a shader's projection uniform is written
    // once, at creation, and never touched again.
    REQUIRE(viewport.projection() == before);
    REQUIRE(viewport.framebuffer_size() == glm::uvec2{640, 2000});
}
