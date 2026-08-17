#include <catch2/catch_test_macros.hpp>

#include "cpen/render/buffer.hh"
#include "cpen/render/vertex_array.hh"
#include "support/gl_fixture.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

using cpen::log::Level;
using cpen::render::Buffer;
using cpen::render::BufferTarget;
using cpen::render::BufferUsage;
using cpen::render::VertexArray;
using cpen::test::gl_context;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;

namespace
{
    constexpr std::array<float, 6> VERTEX_DATA = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    constexpr std::array<std::uint32_t, 3> INDEX_DATA = {0, 1, 2};

    /// Reads a buffer's contents back from the driver.
    ///
    /// Asserting on what the store actually holds is the only way to tell a working
    /// upload from one that quietly wrote nothing: GL reports neither, and a draw
    /// from an empty buffer produces geometry that merely looks wrong.
    template <typename Element, std::size_t Count>
    std::array<Element, Count> read_back(const Buffer& buffer,
                                         const std::size_t offset_in_bytes = 0)
    {
        std::array<Element, Count> result{};

        glBindBuffer(GL_ARRAY_BUFFER, buffer.id());
        glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset_in_bytes),
                           static_cast<GLsizeiptr>(sizeof(result)), result.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        return result;
    }
}

TEST_CASE("a vertex buffer records the size, target and usage it was made with",
          "[render][buffer][gpu]")
{
    gl_context();

    const Buffer buffer = Buffer::vertex(VERTEX_DATA);

    trace("buffer {} holds {} byte(s)", buffer.id(), buffer.size_in_bytes());

    CHECK(buffer.id() != 0);
    CHECK(glIsBuffer(buffer.id()) == GL_TRUE);
    CHECK(buffer.size_in_bytes() == sizeof(VERTEX_DATA));
    CHECK(buffer.target() == BufferTarget::VERTEX);
    CHECK(buffer.usage() == BufferUsage::STATIC);
}

TEST_CASE("an index buffer is made the same way and knows what it is",
          "[render][buffer][gpu]")
{
    gl_context();

    const Buffer buffer = Buffer::index(INDEX_DATA, BufferUsage::DYNAMIC);

    CHECK(buffer.target() == BufferTarget::INDEX);
    CHECK(buffer.usage() == BufferUsage::DYNAMIC);
    CHECK(buffer.size_in_bytes() == sizeof(INDEX_DATA));

    const auto contents = read_back<std::uint32_t, 3>(buffer);
    trace("index buffer {} reads back as {} {} {}", buffer.id(),
          contents[0], contents[1], contents[2]);

    CHECK(contents == INDEX_DATA);
}

TEST_CASE("the store is filled from any contiguous range", "[render][buffer][gpu]")
{
    gl_context();

    const Buffer buffer = Buffer::vertex(VERTEX_DATA);

    const auto contents = read_back<float, 6>(buffer);
    trace("first and last elements read back as {} and {}", contents[0], contents[5]);

    CHECK(contents == VERTEX_DATA);
}

TEST_CASE("a storage buffer is allocated but not filled", "[render][buffer][gpu]")
{
    gl_context();

    const Buffer buffer = Buffer::vertex_storage(256, BufferUsage::STREAM);

    trace("buffer {} reserved {} byte(s) with no contents", buffer.id(),
          buffer.size_in_bytes());

    CHECK(glIsBuffer(buffer.id()) == GL_TRUE);
    CHECK(buffer.size_in_bytes() == 256);
    CHECK(buffer.usage() == BufferUsage::STREAM);
}

TEST_CASE("update overwrites part of the store and leaves its size alone",
          "[render][buffer][gpu]")
{
    gl_context();

    Buffer buffer = Buffer::vertex(VERTEX_DATA, BufferUsage::DYNAMIC);

    constexpr std::array<float, 2> replacement = {-1.0f, -2.0f};

    // Written at the third element, so the test would notice an offset that was
    // taken as an element index rather than as the byte count it is.
    buffer.update(replacement, 2 * sizeof(float));

    const auto contents = read_back<float, 6>(buffer);
    trace("after the update the store reads {} {} {} {} {} {}",
          contents[0], contents[1], contents[2], contents[3], contents[4], contents[5]);

    CHECK(contents[0] == 1.0f);
    CHECK(contents[1] == 2.0f);
    CHECK(contents[2] == -1.0f);
    CHECK(contents[3] == -2.0f);
    CHECK(contents[4] == 5.0f);
    CHECK(buffer.size_in_bytes() == sizeof(VERTEX_DATA));
}

TEST_CASE("an update that would not fit is reported and dropped", "[render][buffer][gpu]")
{
    gl_context();

    Buffer buffer = Buffer::vertex_storage(8, BufferUsage::DYNAMIC);

    const LogCaptureGuard capture;

    SECTION("too much data")
    {
        constexpr std::array<float, 4> overlong = {1.0f, 2.0f, 3.0f, 4.0f};
        buffer.update(overlong);
    }

    SECTION("an offset past the end")
    {
        constexpr std::array<float, 1> single = {1.0f};
        buffer.update(single, 64);
    }

    SECTION("an offset that overflows on addition")
    {
        // offset + size wraps round to something small; checking the other way
        // round, size against the room left, is what keeps this out.
        constexpr std::array<float, 1> single = {1.0f};
        buffer.update(single, static_cast<std::size_t>(-1));
    }

    trace("the refused write produced {} error(s)", capture.count(Level::ERROR));

    CHECK(capture.count(Level::ERROR) == 1);
    CHECK(capture.contains("ignored"));
}

TEST_CASE("an empty update does nothing at all", "[render][buffer][gpu]")
{
    gl_context();

    Buffer buffer = Buffer::vertex_storage(8, BufferUsage::DYNAMIC);

    const LogCaptureGuard capture;

    constexpr std::array<float, 0> nothing = {};
    buffer.update(nothing);

    CHECK(capture.size() == 0);
}

TEST_CASE("moving a buffer transfers the store without deleting it",
          "[render][buffer][gpu]")
{
    gl_context();

    Buffer original = Buffer::vertex(VERTEX_DATA);

    const unsigned int name = original.id();

    {
        Buffer moved{std::move(original)};

        trace("after move: source id {}, destination id {}", original.id(), moved.id());

        CHECK(moved.id() == name);
        CHECK(moved.size_in_bytes() == sizeof(VERTEX_DATA));

        // The moved-from buffer must have given up ownership, or the two would
        // both delete the same store.
        CHECK(original.id() == 0);
        CHECK(original.size_in_bytes() == 0);
    }

    trace("destination destroyed, buffer {} should now be gone", name);
    CHECK(glIsBuffer(name) == GL_FALSE);
}

TEST_CASE("a moved-from buffer deletes nothing when it goes out of scope",
          "[render][buffer][gpu]")
{
    gl_context();

    Buffer survivor = Buffer::vertex(VERTEX_DATA);
    unsigned int name = 0;

    {
        Buffer temporary = Buffer::vertex(VERTEX_DATA);
        name = temporary.id();
        survivor = std::move(temporary);
    }

    trace("buffer {} outlived the scope of the buffer it was moved out of", name);

    CHECK(survivor.id() == name);
    CHECK(glIsBuffer(name) == GL_TRUE);
}

TEST_CASE("creating a buffer leaves a bound vertex array's index binding alone",
          "[render][buffer][gpu]")
{
    gl_context();

    VertexArray array;
    const Buffer first = Buffer::index(INDEX_DATA);
    array.set_index_buffer(first);

    // With the array bound, filling a new index buffer through
    // GL_ELEMENT_ARRAY_BUFFER would rewrite this array's index source and then
    // clear it on unbinding, because that binding is vertex array state rather
    // than context state. Every store is filled through GL_ARRAY_BUFFER precisely
    // so that it cannot.
    array.bind();
    const Buffer second = Buffer::index(INDEX_DATA);

    GLint bound = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound);
    VertexArray::unbind();

    trace("array {} still points at index buffer {} after buffer {} was created",
          array.id(), bound, second.id());

    CHECK(static_cast<unsigned int>(bound) == first.id());
    CHECK(array.index_buffer_id() == first.id());
}

TEST_CASE("orphaning replaces the store and keeps everything else",
          "[render][buffer][gpu]")
{
    gl_context();

    Buffer buffer = Buffer::vertex(VERTEX_DATA, BufferUsage::STREAM);

    const unsigned int name = buffer.id();
    const std::size_t size = buffer.size_in_bytes();

    buffer.orphan();

    trace("buffer {} orphaned: still {} byte(s), still a {} buffer, still {}", buffer.id(),
          buffer.size_in_bytes(), to_string(buffer.target()), to_string(buffer.usage()));

    // The name survives, which is the property the whole idiom rests on: a vertex
    // array holds the buffer's name, so an orphan that changed it would silently
    // detach every array reading from this buffer.
    CHECK(buffer.id() == name);
    CHECK(glIsBuffer(name) == GL_TRUE);
    CHECK(buffer.size_in_bytes() == size);
    CHECK(buffer.target() == BufferTarget::VERTEX);
    CHECK(buffer.usage() == BufferUsage::STREAM);

    SECTION("and the fresh store takes an ordinary update")
    {
        constexpr std::array<float, 6> REPLACEMENT = {9.0f, 8.0f, 7.0f, 6.0f, 5.0f, 4.0f};
        buffer.update(REPLACEMENT);

        const auto read = read_back<float, 6>(buffer);
        trace("after orphan and update: {} {} {} {} {} {}", read[0], read[1], read[2],
              read[3], read[4], read[5]);

        CHECK(read == REPLACEMENT);
    }
}

TEST_CASE("orphaning an index buffer leaves a bound vertex array alone",
          "[render][buffer][gpu]")
{
    gl_context();

    VertexArray array;
    Buffer indices = Buffer::index(INDEX_DATA);
    array.set_index_buffer(indices);

    // The same trap creation has to avoid: orphaning goes through GL_ARRAY_BUFFER,
    // so it cannot disturb the element array binding this array is holding.
    array.bind();
    indices.orphan();

    GLint bound = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound);
    VertexArray::unbind();

    trace("array {} still points at index buffer {} after it was orphaned", array.id(),
          bound);

    CHECK(static_cast<unsigned int>(bound) == indices.id());
    CHECK(array.index_buffer_id() == indices.id());
}
