#include <catch2/catch_test_macros.hpp>

#include "cpen/render/buffer.hh"
#include "cpen/render/draw.hh"
#include "cpen/render/shader.hh"
#include "cpen/render/vertex_array.hh"
#include "support/gl_fixture.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>

using cpen::log::Level;
using cpen::render::AttributeType;
using cpen::render::Buffer;
using cpen::render::IndexType;
using cpen::render::Primitive;
using cpen::render::Shader;
using cpen::render::VertexArray;
using cpen::render::VertexLayout;
using cpen::test::gl_context;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;

namespace
{
    constexpr int TARGET_SIZE = 64;

    constexpr glm::vec4 BLACK{0.0f, 0.0f, 0.0f, 1.0f};

    constexpr const char* SOLID_VERTEX_SHADER = R"(#version 330 core
layout (location = 0) in vec2 in_position;

void main()
{
    gl_Position = vec4(in_position, 0.0, 1.0);
}
)";

    constexpr const char* SOLID_FRAGMENT_SHADER = R"(#version 330 core
out vec4 fragment_color;

void main()
{
    fragment_color = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    /// A colour renderbuffer and the framebuffer it is attached to, so that a draw
    /// can be checked by reading the pixels back.
    ///
    /// Deliberately raw GL rather than a render:: type: there is no
    /// render::Framebuffer yet, and inventing one here would mean testing two
    /// things at once. Drawing into the default framebuffer instead would need no
    /// code at all, but its contents are only defined for a window that is actually
    /// on screen, and the fixture's window is hidden.
    class RenderTarget
    {
    public:
        RenderTarget()
        {
            glGetIntegerv(GL_VIEWPORT, this->previous_viewport.data());

            glGenRenderbuffers(1, &this->color_buffer);
            glBindRenderbuffer(GL_RENDERBUFFER, this->color_buffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, TARGET_SIZE, TARGET_SIZE);

            glGenFramebuffers(1, &this->framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, this->framebuffer);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, this->color_buffer);

            cpen::render::set_viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
        }

        ~RenderTarget()
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &this->framebuffer);
            glDeleteRenderbuffers(1, &this->color_buffer);

            // Restored rather than left at the target's size: the fixture's context
            // is shared by every case in the process, and global state left behind
            // here would surface as an unrelated failure later.
            glViewport(this->previous_viewport[0], this->previous_viewport[1],
                       this->previous_viewport[2], this->previous_viewport[3]);
        }

        RenderTarget(const RenderTarget&) = delete;
        RenderTarget& operator=(const RenderTarget&) = delete;

        bool is_complete() const
        {
            return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }

        std::array<std::uint8_t, 4> pixel_at(const int x, const int y) const
        {
            std::array<std::uint8_t, 4> color{};
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
            return color;
        }

        /// The pixel in the middle of the target, which every case here arranges to
        /// be covered by geometry when the draw worked.
        std::array<std::uint8_t, 4> centre_pixel() const
        {
            return this->pixel_at(TARGET_SIZE / 2, TARGET_SIZE / 2);
        }

    private:
        GLuint framebuffer = 0;
        GLuint color_buffer = 0;
        std::array<GLint, 4> previous_viewport{};
    };

    /// Adds a per-instance offset to the vertex position, so that where an instance
    /// lands is decided entirely by the attribute carrying the divisor.
    constexpr const char* INSTANCED_VERTEX_SHADER = R"(#version 330 core
layout (location = 0) in vec2 in_position;
layout (location = 1) in vec2 in_offset;

void main()
{
    gl_Position = vec4(in_position + in_offset, 0.0, 1.0);
}
)";

    VertexLayout position_layout()
    {
        return VertexLayout{
            .attributes = {{.type = AttributeType::FLOAT, .component_count = 2}},
        };
    }

    void trace_pixel(const char* const label, const std::array<std::uint8_t, 4>& color)
    {
        trace("{}: r {}, g {}, b {}", label, static_cast<int>(color[0]),
              static_cast<int>(color[1]), static_cast<int>(color[2]));
    }
}

TEST_CASE("draw_arrays puts geometry on the framebuffer", "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    // Wide enough to cover the middle of the target and nowhere near its top-left
    // corner, so that a working draw and a missing one differ at more than one
    // pixel and a stray full-screen clear could not pass for either.
    constexpr std::array<float, 6> POSITIONS = {
        -0.8f, -0.8f,
         0.8f, -0.8f,
         0.0f,  0.8f,
    };

    const Buffer vertices = Buffer::vertex(POSITIONS);

    VertexArray array;
    array.attach(vertices, position_layout());

    const auto shader = Shader::create("test.solid", SOLID_VERTEX_SHADER,
                                       SOLID_FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    cpen::render::clear(BLACK);
    shader->bind();
    cpen::render::draw_arrays(array, Primitive::TRIANGLES, 3);
    Shader::unbind();

    const auto centre = target.centre_pixel();
    const auto corner = target.pixel_at(0, TARGET_SIZE - 1);

    trace_pixel("centre", centre);
    trace_pixel("top-left corner", corner);

    CHECK(centre[0] == 255);
    CHECK(centre[1] == 0);
    CHECK(corner[0] == 0);
}

TEST_CASE("draw_elements reads its vertices through the index buffer",
          "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    // Four corners of a quad, drawn as two triangles. Without the indices this
    // geometry is not a quad at all, so a draw that ignored them would leave the
    // centre uncovered.
    constexpr std::array<float, 8> POSITIONS = {
        -0.8f, -0.8f,
         0.8f, -0.8f,
         0.8f,  0.8f,
        -0.8f,  0.8f,
    };

    constexpr std::array<std::uint32_t, 6> INDICES = {0, 1, 2, 2, 3, 0};

    const Buffer vertices = Buffer::vertex(POSITIONS);
    const Buffer indices = Buffer::index(INDICES);

    VertexArray array;
    array.attach(vertices, position_layout());
    array.set_index_buffer(indices);

    const auto shader = Shader::create("test.solid", SOLID_VERTEX_SHADER,
                                       SOLID_FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    cpen::render::clear(BLACK);
    shader->bind();
    cpen::render::draw_elements(array, Primitive::TRIANGLES, INDICES.size());
    Shader::unbind();

    const auto centre = target.centre_pixel();
    trace_pixel("centre", centre);

    CHECK(centre[0] == 255);
}

TEST_CASE("sixteen-bit indices are read as such", "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    constexpr std::array<float, 6> POSITIONS = {
        -0.8f, -0.8f,
         0.8f, -0.8f,
         0.0f,  0.8f,
    };

    // Read as 32-bit these three indices would be two, and the second of them
    // enormous, so getting the type wrong cannot silently produce the same picture.
    constexpr std::array<std::uint16_t, 3> INDICES = {0, 1, 2};

    const Buffer vertices = Buffer::vertex(POSITIONS);
    const Buffer indices = Buffer::index(INDICES);

    VertexArray array;
    array.attach(vertices, position_layout());
    array.set_index_buffer(indices);

    const auto shader = Shader::create("test.solid", SOLID_VERTEX_SHADER,
                                       SOLID_FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    cpen::render::clear(BLACK);
    shader->bind();
    cpen::render::draw_elements(array, Primitive::TRIANGLES, INDICES.size(),
                                IndexType::UNSIGNED_SHORT);
    Shader::unbind();

    const auto centre = target.centre_pixel();
    trace_pixel("centre", centre);

    CHECK(centre[0] == 255);
}

TEST_CASE("an indexed draw without an index buffer is reported and skipped",
          "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    constexpr std::array<float, 6> POSITIONS = {
        -0.8f, -0.8f,
         0.8f, -0.8f,
         0.0f,  0.8f,
    };

    const Buffer vertices = Buffer::vertex(POSITIONS);

    VertexArray array;
    array.attach(vertices, position_layout());

    const auto shader = Shader::create("test.solid", SOLID_VERTEX_SHADER,
                                       SOLID_FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    cpen::render::clear(BLACK);
    shader->bind();

    const LogCaptureGuard capture;
    cpen::render::draw_elements(array, Primitive::TRIANGLES, 3);

    Shader::unbind();

    const auto centre = target.centre_pixel();
    trace("the skipped draw produced {} error(s)", capture.count(Level::ERROR));
    trace_pixel("centre", centre);

    CHECK(capture.count(Level::ERROR) == 1);
    CHECK(centre[0] == 0);
}

TEST_CASE("a draw of nothing is not a draw", "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    constexpr std::array<float, 6> POSITIONS = {
        -0.8f, -0.8f,
         0.8f, -0.8f,
         0.0f,  0.8f,
    };

    const Buffer vertices = Buffer::vertex(POSITIONS);

    VertexArray array;
    array.attach(vertices, position_layout());

    const auto shader = Shader::create("test.solid", SOLID_VERTEX_SHADER,
                                       SOLID_FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    cpen::render::clear(BLACK);
    shader->bind();

    const LogCaptureGuard capture;
    cpen::render::draw_arrays(array, Primitive::TRIANGLES, 0);

    Shader::unbind();

    const auto centre = target.centre_pixel();
    trace_pixel("centre", centre);

    // Silent, unlike the indexed case: an empty batch is a normal frame, whereas
    // an indexed draw with no index buffer is always a mistake.
    CHECK(capture.size() == 0);
    CHECK(centre[0] == 0);
}

TEST_CASE("an instanced draw repeats the geometry once per instance",
          "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    // One small square about the origin, drawn four times. It is deliberately too
    // small to reach the centre of the target from any of the four offsets, so a
    // draw that placed every instance in the same place could not pass: the four
    // quadrants and the centre would then disagree with what is asserted below.
    constexpr std::array<float, 8> POSITIONS = {
        -0.3f, -0.3f,
         0.3f, -0.3f,
         0.3f,  0.3f,
        -0.3f,  0.3f,
    };

    constexpr std::array<std::uint32_t, 6> INDICES = {0, 1, 2, 2, 3, 0};

    constexpr std::array<float, 8> OFFSETS = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f,
    };

    const Buffer vertices = Buffer::vertex(POSITIONS);
    const Buffer indices = Buffer::index(INDICES);
    const Buffer offsets = Buffer::vertex(OFFSETS);

    VertexArray array;
    array.attach(vertices, position_layout());
    array.attach(offsets, VertexLayout{
                              .attributes = {{.type = AttributeType::FLOAT,
                                              .component_count = 2}},
                              .first_location = 1,
                              .instance_divisor = 1,
                          });
    array.set_index_buffer(indices);

    const auto shader = Shader::create("test.instanced", INSTANCED_VERTEX_SHADER,
                                       SOLID_FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    cpen::render::clear(BLACK);
    shader->bind();
    cpen::render::draw_elements_instanced(array, Primitive::TRIANGLES, INDICES.size(), 4);
    Shader::unbind();

    // NDC -0.5 and +0.5 land a quarter and three quarters of the way across a
    // 64-pixel target.
    constexpr int NEAR_QUARTER = TARGET_SIZE / 4;
    constexpr int FAR_QUARTER = TARGET_SIZE - TARGET_SIZE / 4;

    const auto lower_left = target.pixel_at(NEAR_QUARTER, NEAR_QUARTER);
    const auto lower_right = target.pixel_at(FAR_QUARTER, NEAR_QUARTER);
    const auto upper_right = target.pixel_at(FAR_QUARTER, FAR_QUARTER);
    const auto upper_left = target.pixel_at(NEAR_QUARTER, FAR_QUARTER);
    const auto centre = target.centre_pixel();

    trace_pixel("lower left", lower_left);
    trace_pixel("lower right", lower_right);
    trace_pixel("upper right", upper_right);
    trace_pixel("upper left", upper_left);
    trace_pixel("centre, between all four", centre);

    CHECK(lower_left[0] == 255);
    CHECK(lower_right[0] == 255);
    CHECK(upper_right[0] == 255);
    CHECK(upper_left[0] == 255);

    // The gap between the four squares. Without the divisor the offset attribute
    // would advance per vertex instead of per instance, and one misshapen polygon
    // spanning the origin is exactly what that produces.
    CHECK(centre[0] == 0);
}

TEST_CASE("an instanced draw of no instances is not a draw", "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    constexpr std::array<float, 8> POSITIONS = {
        -0.8f, -0.8f,
         0.8f, -0.8f,
         0.8f,  0.8f,
        -0.8f,  0.8f,
    };

    constexpr std::array<std::uint32_t, 6> INDICES = {0, 1, 2, 2, 3, 0};

    const Buffer vertices = Buffer::vertex(POSITIONS);
    const Buffer indices = Buffer::index(INDICES);

    VertexArray array;
    array.attach(vertices, position_layout());
    array.set_index_buffer(indices);

    const auto shader = Shader::create("test.solid", SOLID_VERTEX_SHADER,
                                       SOLID_FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    cpen::render::clear(BLACK);
    shader->bind();

    const LogCaptureGuard capture;
    cpen::render::draw_elements_instanced(array, Primitive::TRIANGLES, INDICES.size(), 0);

    Shader::unbind();

    const auto centre = target.centre_pixel();
    trace_pixel("centre", centre);

    // Silent: an empty batch is a normal frame, exactly as for draw_arrays.
    CHECK(capture.size() == 0);
    CHECK(centre[0] == 0);
}

TEST_CASE("an instanced draw without an index buffer is reported and skipped",
          "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    constexpr std::array<float, 8> POSITIONS = {
        -0.8f, -0.8f,
         0.8f, -0.8f,
         0.8f,  0.8f,
        -0.8f,  0.8f,
    };

    const Buffer vertices = Buffer::vertex(POSITIONS);

    VertexArray array;
    array.attach(vertices, position_layout());

    const auto shader = Shader::create("test.solid", SOLID_VERTEX_SHADER,
                                       SOLID_FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    cpen::render::clear(BLACK);
    shader->bind();

    const LogCaptureGuard capture;
    cpen::render::draw_elements_instanced(array, Primitive::TRIANGLES, 6, 4);

    Shader::unbind();

    const auto centre = target.centre_pixel();
    trace("the skipped draw produced {} error(s)", capture.count(Level::ERROR));
    trace_pixel("centre", centre);

    CHECK(capture.count(Level::ERROR) == 1);
    CHECK(centre[0] == 0);
}

TEST_CASE("clear fills the whole target", "[render][draw][gpu]")
{
    gl_context();

    const RenderTarget target;
    REQUIRE(target.is_complete());

    cpen::render::clear(glm::vec4{0.0f, 1.0f, 0.0f, 1.0f});

    const auto centre = target.centre_pixel();
    const auto corner = target.pixel_at(0, 0);

    trace_pixel("centre", centre);
    trace_pixel("bottom-left corner", corner);

    CHECK(centre[1] == 255);
    CHECK(corner[1] == 255);
    CHECK(centre[0] == 0);
}
