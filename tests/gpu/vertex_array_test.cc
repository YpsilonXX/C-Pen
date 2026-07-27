#include <catch2/catch_test_macros.hpp>

#include "cpen/render/buffer.hh"
#include "cpen/render/vertex_array.hh"
#include "support/gl_fixture.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <utility>

using cpen::log::Level;
using cpen::render::AttributeType;
using cpen::render::Buffer;
using cpen::render::VertexArray;
using cpen::render::VertexLayout;
using cpen::test::gl_context;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;

namespace
{
    /// Position and colour interleaved, the layout the demo triangle uses.
    constexpr std::array<float, 15> INTERLEAVED_DATA = {
        -0.6f, -0.5f,    1.0f, 0.0f, 0.0f,
         0.6f, -0.5f,    0.0f, 1.0f, 0.0f,
         0.0f,  0.6f,    0.0f, 0.0f, 1.0f,
    };

    constexpr std::array<std::uint32_t, 3> INDEX_DATA = {0, 1, 2};

    VertexLayout interleaved_layout(const unsigned int first_location = 0)
    {
        return VertexLayout{
            .attributes = {
                {.type = AttributeType::FLOAT, .component_count = 2},
                {.type = AttributeType::FLOAT, .component_count = 3},
            },
            .first_location = first_location,
        };
    }

    /// Reads back one piece of the attribute state the vertex array recorded.
    ///
    /// These queries answer for whichever array is bound, which is what makes them
    /// worth asserting on: they read the state the driver actually kept, not a copy
    /// the engine happens to hold alongside it.
    GLint attribute_parameter(const VertexArray& array, const unsigned int location,
                              const GLenum parameter)
    {
        array.bind();

        GLint value = 0;
        glGetVertexAttribiv(location, parameter, &value);

        VertexArray::unbind();
        return value;
    }

    std::uintptr_t attribute_offset(const VertexArray& array, const unsigned int location)
    {
        array.bind();

        void* pointer = nullptr;
        glGetVertexAttribPointerv(location, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);

        VertexArray::unbind();
        return reinterpret_cast<std::uintptr_t>(pointer);
    }
}

TEST_CASE("a vertex array exists as soon as it is constructed", "[render][vertex_array][gpu]")
{
    gl_context();

    const VertexArray array;

    trace("vertex array {} created", array.id());

    CHECK(array.id() != 0);

    // glGenVertexArrays only reserves the name — the object is created by the first
    // bind, and glIsVertexArray answers false until then. The constructor does that
    // bind, which is what this asserts.
    CHECK(glIsVertexArray(array.id()) == GL_TRUE);

    CHECK(array.attribute_count() == 0);
    CHECK_FALSE(array.has_index_buffer());
}

TEST_CASE("attaching a buffer enables one slot per attribute", "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer vertices = Buffer::vertex(INTERLEAVED_DATA);

    VertexArray array;
    array.attach(vertices, interleaved_layout());

    trace("array {} enabled {} slot(s) from buffer {}", array.id(),
          array.attribute_count(), vertices.id());

    CHECK(array.attribute_count() == 2);
    CHECK(attribute_parameter(array, 0, GL_VERTEX_ATTRIB_ARRAY_ENABLED) == GL_TRUE);
    CHECK(attribute_parameter(array, 1, GL_VERTEX_ATTRIB_ARRAY_ENABLED) == GL_TRUE);
    CHECK(attribute_parameter(array, 2, GL_VERTEX_ATTRIB_ARRAY_ENABLED) == GL_FALSE);
}

TEST_CASE("the stride and offsets are computed from the layout",
          "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer vertices = Buffer::vertex(INTERLEAVED_DATA);

    VertexArray array;
    array.attach(vertices, interleaved_layout());

    const GLint position_stride = attribute_parameter(array, 0, GL_VERTEX_ATTRIB_ARRAY_STRIDE);
    const GLint color_stride = attribute_parameter(array, 1, GL_VERTEX_ATTRIB_ARRAY_STRIDE);

    trace("both slots share a stride of {} byte(s); colour begins at byte {}",
          position_stride, attribute_offset(array, 1));

    // Interleaved, so both attributes carry the whole vertex as their stride.
    CHECK(position_stride == 20);
    CHECK(color_stride == 20);

    CHECK(attribute_parameter(array, 0, GL_VERTEX_ATTRIB_ARRAY_SIZE) == 2);
    CHECK(attribute_parameter(array, 1, GL_VERTEX_ATTRIB_ARRAY_SIZE) == 3);

    CHECK(attribute_offset(array, 0) == 0);
    CHECK(attribute_offset(array, 1) == 2 * sizeof(float));
}

TEST_CASE("the buffer an attribute reads from is recorded in the array",
          "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer vertices = Buffer::vertex(INTERLEAVED_DATA);

    VertexArray array;
    array.attach(vertices, interleaved_layout());

    const GLint bound =
        attribute_parameter(array, 0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING);

    trace("slot 0 of array {} reads from buffer {}", array.id(), bound);

    // This is the reason VertexArray need not own the buffer, and equally the
    // reason the buffer must outlive it: the array holds the name, nothing more.
    CHECK(static_cast<unsigned int>(bound) == vertices.id());
}

TEST_CASE("attributes start at the layout's first location", "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer vertices = Buffer::vertex(INTERLEAVED_DATA);

    VertexArray array;
    array.attach(vertices, interleaved_layout(3));

    trace("the two attributes were placed at slots 3 and 4");

    CHECK(attribute_parameter(array, 0, GL_VERTEX_ATTRIB_ARRAY_ENABLED) == GL_FALSE);
    CHECK(attribute_parameter(array, 3, GL_VERTEX_ATTRIB_ARRAY_ENABLED) == GL_TRUE);
    CHECK(attribute_parameter(array, 4, GL_VERTEX_ATTRIB_ARRAY_ENABLED) == GL_TRUE);
}

TEST_CASE("a second buffer can feed further slots of the same array",
          "[render][vertex_array][gpu]")
{
    gl_context();

    constexpr std::array<float, 3> weights = {0.25f, 0.5f, 1.0f};

    const Buffer positions = Buffer::vertex(INTERLEAVED_DATA);
    const Buffer extra = Buffer::vertex(weights);

    VertexArray array;
    array.attach(positions, interleaved_layout());
    array.attach(extra, VertexLayout{
                            .attributes = {{.type = AttributeType::FLOAT,
                                            .component_count = 1}},
                            .first_location = 2,
                        });

    trace("array {} now reads {} slot(s) from two buffers", array.id(),
          array.attribute_count());

    CHECK(array.attribute_count() == 3);
    CHECK(static_cast<unsigned int>(
              attribute_parameter(array, 1, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING)) ==
          positions.id());
    CHECK(static_cast<unsigned int>(
              attribute_parameter(array, 2, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING)) ==
          extra.id());
}

TEST_CASE("an index buffer is recorded into the array", "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer indices = Buffer::index(INDEX_DATA);

    VertexArray array;
    CHECK_FALSE(array.has_index_buffer());

    array.set_index_buffer(indices);

    array.bind();
    GLint bound = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound);
    VertexArray::unbind();

    trace("array {} records index buffer {}, and GL agrees at {}", array.id(),
          array.index_buffer_id(), bound);

    CHECK(array.has_index_buffer());
    CHECK(array.index_buffer_id() == indices.id());
    CHECK(static_cast<unsigned int>(bound) == indices.id());
}

TEST_CASE("a buffer offered for the wrong role is refused", "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer vertices = Buffer::vertex(INTERLEAVED_DATA);
    const Buffer indices = Buffer::index(INDEX_DATA);

    VertexArray array;
    const LogCaptureGuard capture;

    SECTION("an index buffer where attribute data was meant")
    {
        array.attach(indices, interleaved_layout());
        CHECK(array.attribute_count() == 0);
    }

    SECTION("a vertex buffer where indices were meant")
    {
        array.set_index_buffer(vertices);
        CHECK_FALSE(array.has_index_buffer());
    }

    trace("the mix-up produced {} error(s)", capture.count(Level::ERROR));

    CHECK(capture.count(Level::ERROR) == 1);
    CHECK(capture.contains("ignored"));
}

TEST_CASE("an empty layout is refused", "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer vertices = Buffer::vertex(INTERLEAVED_DATA);

    VertexArray array;
    const LogCaptureGuard capture;

    array.attach(vertices, VertexLayout{});

    trace("attaching with no attributes produced {} error(s)", capture.count(Level::ERROR));

    CHECK(capture.count(Level::ERROR) == 1);
    CHECK(array.attribute_count() == 0);
}

TEST_CASE("an attribute wider than a slot is reported and skipped",
          "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer vertices = Buffer::vertex(INTERLEAVED_DATA);

    VertexArray array;
    const LogCaptureGuard capture;

    // A mat4 is four slots, not one attribute of sixteen components; asking for it
    // this way is a mistake GL answers with GL_INVALID_VALUE and no geometry.
    array.attach(vertices, VertexLayout{
                               .attributes = {
                                   {.type = AttributeType::FLOAT, .component_count = 16},
                                   {.type = AttributeType::FLOAT, .component_count = 2},
                               },
                           });

    // One error, not one record: the attach still logs its usual debug summary
    // for the part of the layout that did land.
    trace("the oversized attribute produced {} error(s); {} slot(s) survived",
          capture.count(Level::ERROR), array.attribute_count());

    CHECK(capture.count(Level::ERROR) == 1);

    // The bad slot is skipped, the rest of the layout still lands — and it lands at
    // the slot it was written for, because the skipped attribute still advances the
    // location and the offset.
    CHECK(array.attribute_count() == 1);
    CHECK(attribute_parameter(array, 0, GL_VERTEX_ATTRIB_ARRAY_ENABLED) == GL_FALSE);
    CHECK(attribute_parameter(array, 1, GL_VERTEX_ATTRIB_ARRAY_ENABLED) == GL_TRUE);
}

TEST_CASE("moving a vertex array transfers the object without deleting it",
          "[render][vertex_array][gpu]")
{
    gl_context();

    const Buffer vertices = Buffer::vertex(INTERLEAVED_DATA);
    const Buffer indices = Buffer::index(INDEX_DATA);

    VertexArray original;
    original.attach(vertices, interleaved_layout());
    original.set_index_buffer(indices);

    const unsigned int name = original.id();

    {
        VertexArray moved{std::move(original)};

        trace("after move: source id {}, destination id {}", original.id(), moved.id());

        CHECK(moved.id() == name);
        CHECK(moved.attribute_count() == 2);
        CHECK(moved.index_buffer_id() == indices.id());

        CHECK(original.id() == 0);
        CHECK(original.attribute_count() == 0);
        CHECK_FALSE(original.has_index_buffer());
    }

    trace("destination destroyed, vertex array {} should now be gone", name);
    CHECK(glIsVertexArray(name) == GL_FALSE);
}
