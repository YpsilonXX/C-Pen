#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "cpen/render/sprite.hh"
#include "support/trace.hh"

#include <glm/glm.hpp>

#include <numbers>

using cpen::render::normalized_region;
using cpen::render::Sprite;
using cpen::render::sprite_transform;
using cpen::render::SpriteTransform;
using cpen::render::TextureRegion;
using cpen::test::trace;
using cpen::test::trace_step;
using Catch::Matchers::WithinAbs;

// The sprite transform is arithmetic over a description and touches no GL, which is
// the whole reason it is a free function rather than a private step inside
// SpriteBatch: where a sprite ends up can be asserted by naming its four corners
// instead of by drawing it and reading pixels back.

namespace
{
    constexpr float TOLERANCE = 0.001f;

    /// Corners of the unit quad, in the order the batch's index buffer assumes.
    constexpr glm::vec2 TOP_LEFT{0.0f, 0.0f};
    constexpr glm::vec2 TOP_RIGHT{1.0f, 0.0f};
    constexpr glm::vec2 BOTTOM_RIGHT{1.0f, 1.0f};
    constexpr glm::vec2 BOTTOM_LEFT{0.0f, 1.0f};

    void require_point(const glm::vec2 actual, const float x, const float y)
    {
        REQUIRE_THAT(actual.x, WithinAbs(x, TOLERANCE));
        REQUIRE_THAT(actual.y, WithinAbs(y, TOLERANCE));
    }

    void trace_corners(const SpriteTransform& transform)
    {
        for (const auto& [name, corner] :
             {std::pair{"top left", TOP_LEFT}, std::pair{"top right", TOP_RIGHT},
              std::pair{"bottom right", BOTTOM_RIGHT}, std::pair{"bottom left", BOTTOM_LEFT}})
        {
            const glm::vec2 landed = transform.apply(corner);
            trace("{}: ({}, {})", name, landed.x, landed.y);
        }
    }
}

TEST_CASE("an unrotated sprite spans its position and size", "[render][sprite]")
{
    const Sprite sprite{
        .position = {100.0f, 200.0f},
        .size = {640.0f, 480.0f},
    };

    const SpriteTransform transform = sprite_transform(sprite);
    trace_corners(transform);

    // The default origin is the top-left corner, so position is that corner and the
    // sprite extends right and *down* — y grows downwards in virtual space.
    require_point(transform.apply(TOP_LEFT), 100.0f, 200.0f);
    require_point(transform.apply(TOP_RIGHT), 740.0f, 200.0f);
    require_point(transform.apply(BOTTOM_RIGHT), 740.0f, 680.0f);
    require_point(transform.apply(BOTTOM_LEFT), 100.0f, 680.0f);
}

TEST_CASE("the origin decides which point of the sprite sits at its position",
          "[render][sprite]")
{
    Sprite sprite{
        .position = {960.0f, 540.0f},
        .size = {200.0f, 100.0f},
    };

    SECTION("centred")
    {
        sprite.origin = {0.5f, 0.5f};

        const SpriteTransform transform = sprite_transform(sprite);
        trace_corners(transform);

        require_point(transform.apply(TOP_LEFT), 860.0f, 490.0f);
        require_point(transform.apply(BOTTOM_RIGHT), 1060.0f, 590.0f);
    }

    SECTION("standing on its feet")
    {
        // How a character sprite is placed: the middle of the bottom edge is the
        // point on the floor the character occupies, so the artwork's height never
        // moves where they are standing.
        sprite.origin = {0.5f, 1.0f};

        const SpriteTransform transform = sprite_transform(sprite);
        trace_corners(transform);

        require_point(transform.apply(BOTTOM_LEFT), 860.0f, 540.0f);
        require_point(transform.apply(BOTTOM_RIGHT), 1060.0f, 540.0f);
        require_point(transform.apply(TOP_LEFT), 860.0f, 440.0f);
    }

    SECTION("and the size can change under it without moving the anchor")
    {
        sprite.origin = {0.5f, 1.0f};
        const glm::vec2 before = sprite_transform(sprite).apply({0.5f, 1.0f});

        sprite.size = {400.0f, 700.0f};
        const glm::vec2 after = sprite_transform(sprite).apply({0.5f, 1.0f});

        trace("anchor before ({}, {}), after ({}, {})", before.x, before.y, after.x,
              after.y);

        // The origin is a fraction rather than a length precisely so that this
        // holds: rescaling artwork must not slide the character across the floor.
        require_point(after, before.x, before.y);
    }
}

TEST_CASE("a positive rotation turns the sprite clockwise on screen", "[render][sprite]")
{
    constexpr float QUARTER_TURN = std::numbers::pi_v<float> / 2.0f;

    const Sprite sprite{
        .position = {0.0f, 0.0f},
        .size = {100.0f, 50.0f},
        .rotation = QUARTER_TURN,
    };

    const SpriteTransform transform = sprite_transform(sprite);
    trace_corners(transform);

    trace_step("the edge that pointed right now points down the screen");

    // A quarter turn takes the sprite's local +x, which was to the right, onto the
    // screen's +y, which is downwards. Right to down is clockwise as seen. This is
    // not a convention chosen here: it follows from y growing downwards, and it is
    // the reason Sprite::rotation says so out loud.
    require_point(transform.apply(TOP_RIGHT), 0.0f, 100.0f);
    require_point(transform.apply(BOTTOM_LEFT), -50.0f, 0.0f);
}

TEST_CASE("rotation moves the sprite without resizing it", "[render][sprite]")
{
    const Sprite sprite{
        .position = {500.0f, 500.0f},
        .size = {300.0f, 120.0f},
        .origin = {0.5f, 0.5f},
        .rotation = 0.7f,
    };

    const SpriteTransform transform = sprite_transform(sprite);
    trace_corners(transform);

    const float width = glm::length(transform.apply(TOP_RIGHT) - transform.apply(TOP_LEFT));
    const float height =
        glm::length(transform.apply(BOTTOM_LEFT) - transform.apply(TOP_LEFT));

    trace("rotated edges measure {} x {}", width, height);

    REQUIRE_THAT(width, WithinAbs(300.0f, TOLERANCE));
    REQUIRE_THAT(height, WithinAbs(120.0f, TOLERANCE));

    trace_step("and a centred origin stays exactly where it was put");
    require_point(transform.apply({0.5f, 0.5f}), 500.0f, 500.0f);
}

TEST_CASE("an unrotated sprite takes the branch with no trigonometry", "[render][sprite]")
{
    // Not observable from outside except as exactness: cos(0) and sin(0) are exact
    // in floating point, so the fast path is not detectable by a different answer.
    // What this case pins down is that the answer is the same either way, which is
    // the property that makes the branch safe to take.
    Sprite sprite{
        .position = {10.0f, 20.0f},
        .size = {30.0f, 40.0f},
        .origin = {0.25f, 0.75f},
    };

    const SpriteTransform unrotated = sprite_transform(sprite);

    sprite.rotation = 0.0f;
    const SpriteTransform explicitly_zero = sprite_transform(sprite);

    require_point(unrotated.x_axis, explicitly_zero.x_axis.x, explicitly_zero.x_axis.y);
    require_point(unrotated.y_axis, explicitly_zero.y_axis.x, explicitly_zero.y_axis.y);
    require_point(unrotated.translation, explicitly_zero.translation.x,
                  explicitly_zero.translation.y);
}

TEST_CASE("a region in texels becomes the rectangle a sampler takes", "[render][sprite]")
{
    constexpr glm::vec2 TEXTURE_SIZE{256.0f, 128.0f};

    SECTION("the default region is the whole texture")
    {
        const glm::vec4 uv = normalized_region(TextureRegion{}, TEXTURE_SIZE);
        trace("default region: ({}, {}) to ({}, {})", uv.x, uv.y, uv.z, uv.w);

        REQUIRE_THAT(uv.x, WithinAbs(0.0f, TOLERANCE));
        REQUIRE_THAT(uv.y, WithinAbs(0.0f, TOLERANCE));
        REQUIRE_THAT(uv.z, WithinAbs(1.0f, TOLERANCE));
        REQUIRE_THAT(uv.w, WithinAbs(1.0f, TOLERANCE));
    }

    SECTION("a sub-rectangle divides by the texture's own size per axis")
    {
        const TextureRegion region{.position = {64.0f, 32.0f}, .size = {128.0f, 64.0f}};

        const glm::vec4 uv = normalized_region(region, TEXTURE_SIZE);
        trace("region: ({}, {}) to ({}, {})", uv.x, uv.y, uv.z, uv.w);

        REQUIRE_THAT(uv.x, WithinAbs(0.25f, TOLERANCE));
        REQUIRE_THAT(uv.y, WithinAbs(0.25f, TOLERANCE));
        REQUIRE_THAT(uv.z, WithinAbs(0.75f, TOLERANCE));
        REQUIRE_THAT(uv.w, WithinAbs(0.75f, TOLERANCE));
    }

    SECTION("a texture with no size falls back to the whole thing")
    {
        // Guarded because the alternative is a division by zero, and a NaN texture
        // coordinate draws nothing at all while reporting nothing at all.
        const TextureRegion region{.position = {0.0f, 0.0f}, .size = {16.0f, 16.0f}};

        const glm::vec4 uv = normalized_region(region, glm::vec2{0.0f, 0.0f});
        trace("degenerate texture: ({}, {}) to ({}, {})", uv.x, uv.y, uv.z, uv.w);

        REQUIRE_THAT(uv.z, WithinAbs(1.0f, TOLERANCE));
        REQUIRE_THAT(uv.w, WithinAbs(1.0f, TOLERANCE));
    }
}
