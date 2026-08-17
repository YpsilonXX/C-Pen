#include <catch2/catch_test_macros.hpp>

#include "cpen/render/vertex_array.hh"
#include "support/trace.hh"

using cpen::render::AttributeType;
using cpen::render::component_size;
using cpen::render::VertexLayout;
using cpen::test::trace;

// VertexLayout is pure arithmetic over a description, so these cases belong in the
// suite that runs anywhere rather than in the GPU target next to the vertex array
// they describe.

TEST_CASE("a component's size follows its scalar type", "[render][layout]")
{
    STATIC_REQUIRE(component_size(AttributeType::FLOAT) == 4);
    STATIC_REQUIRE(component_size(AttributeType::UNSIGNED_BYTE) == 1);
    STATIC_REQUIRE(component_size(AttributeType::INTEGER) == 4);
    STATIC_REQUIRE(component_size(AttributeType::UNSIGNED_INTEGER) == 4);
}

TEST_CASE("the stride is the total size of the interleaved attributes", "[render][layout]")
{
    SECTION("position and colour as floats")
    {
        const VertexLayout layout{
            .attributes = {
                {.type = AttributeType::FLOAT, .component_count = 2},
                {.type = AttributeType::FLOAT, .component_count = 3},
            },
        };

        trace("2 floats + 3 floats gives a stride of {} byte(s)", layout.stride());
        CHECK(layout.stride() == 20);
    }

    SECTION("a packed colour costs a quarter of what four floats cost")
    {
        const VertexLayout layout{
            .attributes = {
                {.type = AttributeType::FLOAT, .component_count = 2},
                {.type = AttributeType::UNSIGNED_BYTE, .component_count = 4, .normalized = true},
            },
        };

        trace("2 floats + 4 normalised bytes gives a stride of {} byte(s)", layout.stride());
        CHECK(layout.stride() == 12);
    }

    SECTION("an empty layout has no stride")
    {
        const VertexLayout layout;
        CHECK(layout.stride() == 0);
    }
}

TEST_CASE("trailing padding is part of the stride", "[render][layout]")
{
    // The sprite batch's instance record: two vec4s, a vec2 and a packed colour
    // come to 44 bytes, and four bytes of padding round the stride up to a
    // multiple of sixteen. The attributes are unaffected — only the distance to
    // the next record changes — which is the whole point of expressing it here
    // rather than as a fifth attribute nothing reads.
    const VertexLayout instances{
        .attributes = {
            {.type = AttributeType::FLOAT, .component_count = 4},
            {.type = AttributeType::FLOAT, .component_count = 4},
            {.type = AttributeType::FLOAT, .component_count = 2},
            {.type = AttributeType::UNSIGNED_BYTE, .component_count = 4, .normalized = true},
        },
        .instance_divisor = 1,
        .trailing_padding = 4,
    };

    trace("44 bytes of attributes + 4 of padding gives a stride of {}", instances.stride());

    CHECK(instances.stride() == 48);
    CHECK(instances.stride() % 16 == 0);

    SECTION("and without it the same attributes stop at their own total")
    {
        VertexLayout unpadded = instances;
        unpadded.trailing_padding = 0;

        CHECK(unpadded.stride() == 44);
    }
}

TEST_CASE("an attribute reports the room it takes in a vertex", "[render][layout]")
{
    constexpr cpen::render::VertexAttribute position{
        .type = AttributeType::FLOAT,
        .component_count = 3,
    };

    STATIC_REQUIRE(position.size_in_bytes() == 12);
}
