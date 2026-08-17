#include <catch2/catch_test_macros.hpp>

#include "cpen/core/error.hh"
#include "cpen/render/shader.hh"
#include "support/gl_fixture.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <glad/glad.h>

#include <string>
#include <utility>

using cpen::core::ErrorCode;
using cpen::render::Shader;
using cpen::test::gl_context;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;

namespace
{
    /// A vertex stage that uses every uniform it declares. "Uses" is the operative
    /// word: a uniform no instruction reads is allowed to be optimised out of the
    /// program entirely, and would then be absent from the uniform table through no
    /// fault of ours.
    constexpr const char* VERTEX_SOURCE = R"(#version 330 core
layout (location = 0) in vec2 in_position;

uniform mat4 projection;
uniform vec4 palette[4];
uniform float scale;

out vec4 vertex_color;

void main()
{
    vertex_color = palette[0] + palette[3];
    gl_Position = projection * vec4(in_position * scale, 0.0, 1.0);
}
)";

    constexpr const char* FRAGMENT_SOURCE = R"(#version 330 core
in vec4 vertex_color;

uniform sampler2D atlas;

out vec4 fragment_color;

void main()
{
    fragment_color = vertex_color * texture(atlas, vec2(0.5, 0.5));
}
)";

    constexpr const char* MINIMAL_VERTEX_SOURCE = R"(#version 330 core
void main()
{
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

    constexpr const char* MINIMAL_FRAGMENT_SOURCE = R"(#version 330 core
out vec4 fragment_color;

void main()
{
    fragment_color = vec4(1.0);
}
)";
}

TEST_CASE("a valid shader links and reports its program", "[render][shader][gpu]")
{
    gl_context();

    const auto shader = Shader::create("test.valid", VERTEX_SOURCE, FRAGMENT_SOURCE);

    REQUIRE(shader.has_value());
    trace("linked '{}' as program {}, {} uniform(s)", shader->name(), shader->id(),
          shader->uniform_count());

    CHECK(shader->name() == "test.valid");
    CHECK(shader->id() != 0);
    CHECK(glIsProgram(shader->id()) == GL_TRUE);
}

TEST_CASE("the uniform table is collected at link time", "[render][shader][gpu]")
{
    gl_context();

    const auto shader = Shader::create("test.uniforms", VERTEX_SOURCE, FRAGMENT_SOURCE);
    REQUIRE(shader.has_value());

    trace("driver kept {} uniform(s)", shader->uniform_count());

    CHECK(shader->has_uniform("projection"));
    CHECK(shader->has_uniform("scale"));
    CHECK(shader->has_uniform("atlas"));

    // The driver reports an array under the name of its first element; the table
    // stores the base name, which is what a caller would think to ask for.
    trace("array uniform is queried as 'palette', not 'palette[0]'");
    CHECK(shader->has_uniform("palette"));
    CHECK_FALSE(shader->has_uniform("palette[0]"));

    CHECK_FALSE(shader->has_uniform("no_such_uniform"));
}

TEST_CASE("a stage that does not compile is reported with its stage named",
          "[render][shader][gpu]")
{
    gl_context();

    constexpr const char* BROKEN = R"(#version 330 core
void main()
{
    this line is not glsl
}
)";

    SECTION("vertex stage")
    {
        const auto shader = Shader::create("test.broken_vertex", BROKEN, MINIMAL_FRAGMENT_SOURCE);

        REQUIRE_FALSE(shader.has_value());
        trace("error: {}", shader.error().message);

        CHECK(shader.error().code == ErrorCode::COMPILATION_FAILED);
        CHECK(shader.error().message.contains("test.broken_vertex"));
        CHECK(shader.error().message.contains("vertex"));
    }

    SECTION("fragment stage")
    {
        const auto shader = Shader::create("test.broken_fragment", MINIMAL_VERTEX_SOURCE, BROKEN);

        REQUIRE_FALSE(shader.has_value());
        trace("error: {}", shader.error().message);

        CHECK(shader.error().code == ErrorCode::COMPILATION_FAILED);
        CHECK(shader.error().message.contains("test.broken_fragment"));
        CHECK(shader.error().message.contains("fragment"));
    }
}

TEST_CASE("a program that cannot be linked is reported", "[render][shader][gpu]")
{
    gl_context();

    // Both stages compile in isolation — a shader without main() is a link-time
    // problem, not a compile-time one. Which of the two messages the driver
    // produces is its own business, so only the failure and the shader's name are
    // asserted here.
    constexpr const char* NO_ENTRY_POINT = R"(#version 330 core
out vec4 fragment_color;

vec4 unused()
{
    return vec4(1.0);
}
)";

    const auto shader = Shader::create("test.unlinkable", MINIMAL_VERTEX_SOURCE, NO_ENTRY_POINT);

    REQUIRE_FALSE(shader.has_value());
    trace("error: {}", shader.error().message);

    CHECK(shader.error().code == ErrorCode::COMPILATION_FAILED);
    CHECK(shader.error().message.contains("test.unlinkable"));
}

TEST_CASE("source length is taken from the view, not from a terminator",
          "[render][shader][gpu]")
{
    gl_context();

    // A view over a longer buffer: everything after the shader source is garbage
    // that must never reach the driver. This is what the explicit length in
    // glShaderSource buys, and nothing else in the suite would notice its absence.
    const std::string buffer = std::string{MINIMAL_FRAGMENT_SOURCE} + "and then some rubbish";
    const std::string_view source{buffer.data(), std::string_view{MINIMAL_FRAGMENT_SOURCE}.size()};

    trace("compiling {} bytes out of a {}-byte buffer", source.size(), buffer.size());

    const auto shader = Shader::create("test.truncated", MINIMAL_VERTEX_SOURCE, source);

    CHECK(shader.has_value());
}

TEST_CASE("moving a shader transfers the program without deleting it",
          "[render][shader][gpu]")
{
    gl_context();

    auto original = Shader::create("test.move", VERTEX_SOURCE, FRAGMENT_SOURCE);
    REQUIRE(original.has_value());

    const unsigned int program = original->id();
    const std::size_t uniform_count = original->uniform_count();

    {
        Shader moved{std::move(*original)};

        trace("after move: source id {}, destination id {}", original->id(), moved.id());

        CHECK(moved.id() == program);
        CHECK(moved.name() == "test.move");
        CHECK(moved.uniform_count() == uniform_count);

        // The moved-from shader must have given up ownership, or the two would
        // both delete the same program.
        CHECK(original->id() == 0);
    }

    trace("destination destroyed, program {} should now be gone", program);
    CHECK(glIsProgram(program) == GL_FALSE);
}

TEST_CASE("a moved-from shader deletes nothing when it goes out of scope",
          "[render][shader][gpu]")
{
    gl_context();

    unsigned int program = 0;

    auto survivor = Shader::create("test.survivor", MINIMAL_VERTEX_SOURCE,
                                   MINIMAL_FRAGMENT_SOURCE);
    REQUIRE(survivor.has_value());

    {
        auto temporary = Shader::create("test.temporary", MINIMAL_VERTEX_SOURCE,
                                        MINIMAL_FRAGMENT_SOURCE);
        REQUIRE(temporary.has_value());

        program = temporary->id();
        *survivor = std::move(*temporary);
    }

    trace("program {} outlived the scope of the shader it was moved out of", program);

    CHECK(survivor->id() == program);
    CHECK(glIsProgram(program) == GL_TRUE);
}

TEST_CASE("writing an unknown uniform is reported once and ignored",
          "[render][shader][gpu]")
{
    gl_context();

    auto shader = Shader::create("test.diagnostics", VERTEX_SOURCE, FRAGMENT_SOURCE);
    REQUIRE(shader.has_value());
    shader->bind();

    const LogCaptureGuard capture;

    shader->set_uniform("no_such_uniform", 1.0f);
    shader->set_uniform("no_such_uniform", 2.0f);
    shader->set_uniform("no_such_uniform", 3.0f);

    trace("three writes to a missing uniform produced {} log record(s)", capture.size());

    // Once, not three times: a missing name is a fixed property of the program, so
    // repeating the complaint every frame would only drown the log.
    CHECK(capture.size() == 1);
    CHECK(capture.contains("no_such_uniform"));
    CHECK(capture.contains("test.diagnostics"));

    Shader::unbind();
}

TEST_CASE("writing a uniform with the wrong type is refused", "[render][shader][gpu]")
{
    gl_context();

    auto shader = Shader::create("test.types", VERTEX_SOURCE, FRAGMENT_SOURCE);
    REQUIRE(shader.has_value());
    shader->bind();

    const LogCaptureGuard capture;

    // 'projection' is a mat4; writing a float into it would otherwise be accepted
    // by GL as a silent no-op against a location that does exist.
    shader->set_uniform("projection", 1.0f);

    trace("float written to a mat4 uniform produced {} log record(s)", capture.size());

    CHECK(capture.size() == 1);
    CHECK(capture.contains("projection"));

    Shader::unbind();
}

TEST_CASE("a sampler is written like an int", "[render][shader][gpu]")
{
    gl_context();

    auto shader = Shader::create("test.sampler", VERTEX_SOURCE, FRAGMENT_SOURCE);
    REQUIRE(shader.has_value());
    shader->bind();

    const LogCaptureGuard capture;

    // A sampler's value is a texture unit index, so glUniform1i is the correct
    // setter even though the declared GL type is GL_SAMPLER_2D rather than GL_INT.
    shader->set_uniform("atlas", 0);

    trace("writing texture unit 0 into a sampler2D produced {} log record(s)", capture.size());

    CHECK(capture.size() == 0);

    Shader::unbind();
}
