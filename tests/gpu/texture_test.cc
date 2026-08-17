#include <catch2/catch_test_macros.hpp>

#include "cpen/core/error.hh"
#include "cpen/render/buffer.hh"
#include "cpen/render/draw.hh"
#include "cpen/render/image.hh"
#include "cpen/render/shader.hh"
#include "cpen/render/texture.hh"
#include "cpen/render/vertex_array.hh"
#include "support/gl_fixture.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

using cpen::core::ErrorCode;
using cpen::log::Level;
using cpen::render::AttributeType;
using cpen::render::Buffer;
using cpen::render::Image;
using cpen::render::image_size_in_bytes;
using cpen::render::PixelFormat;
using cpen::render::Primitive;
using cpen::render::Shader;
using cpen::render::Texture;
using cpen::render::TextureConfig;
using cpen::render::TextureFilter;
using cpen::render::VertexArray;
using cpen::render::VertexLayout;
using cpen::test::gl_context;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    /// A 2x2 RGBA image: red and green on the first row uploaded, blue and white
    /// on the second.
    ///
    /// Four different colours rather than a symmetric pattern, because every
    /// orientation question this file asks — which row GL calls the first one,
    /// which corner a texture coordinate of (0, 0) lands in — is only answerable
    /// with an image that is the same in no two directions.
    constexpr std::array<std::uint8_t, 16> QUAD_PIXELS = {
        255, 0,   0,   255,    0,   255, 0,   255,
        0,   0,   255, 255,    255, 255, 255, 255,
    };

    std::span<const std::byte> bytes_of(const std::span<const std::uint8_t> values)
    {
        return std::as_bytes(values);
    }

    GLenum to_gl_source_format(const PixelFormat format)
    {
        switch (format)
        {
            case PixelFormat::R8:    return GL_RED;
            case PixelFormat::RGB8:  return GL_RGB;
            case PixelFormat::RGBA8: return GL_RGBA;
        }
        return GL_RGBA;
    }

    /// Reads a texture's contents back from the driver.
    ///
    /// The only way to tell a working upload from one that quietly wrote nothing
    /// or wrote it skewed: GL reports neither, and a draw from a wrong texture
    /// still produces a picture.
    ///
    /// GL_PACK_ALIGNMENT is the mirror image of the GL_UNPACK_ALIGNMENT the
    /// texture code sets on the way in, and has to be dealt with here for the same
    /// reason — otherwise a three-byte row is read back as if it were four, and
    /// this helper would report a fault that only it has.
    std::vector<std::uint8_t> read_back(const Texture& texture)
    {
        std::vector<std::uint8_t> pixels(
            image_size_in_bytes(texture.width(), texture.height(), texture.format()));

        GLint previous_alignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &previous_alignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);

        glBindTexture(GL_TEXTURE_2D, texture.id());
        glGetTexImage(GL_TEXTURE_2D, 0, to_gl_source_format(texture.format()),
                      GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        glPixelStorei(GL_PACK_ALIGNMENT, previous_alignment);

        return pixels;
    }

    /// The texture currently bound to `unit`, without disturbing which unit is
    /// active afterwards.
    GLint binding_of(const unsigned int unit)
    {
        GLint active_unit = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_unit);

        glActiveTexture(GL_TEXTURE0 + unit);

        GLint binding = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);

        glActiveTexture(static_cast<GLenum>(active_unit));

        return binding;
    }
}

TEST_CASE("a texture records the size and format it was made with",
          "[render][texture][gpu]")
{
    gl_context();

    const auto texture = Texture::from_pixels(bytes_of(QUAD_PIXELS), 2, 2, PixelFormat::RGBA8);
    REQUIRE(texture.has_value());

    trace("texture {} is {}x{} {}", texture->id(), texture->width(), texture->height(),
          to_string(texture->format()));

    CHECK(texture->id() != 0);
    CHECK(glIsTexture(texture->id()) == GL_TRUE);
    CHECK(texture->width() == 2);
    CHECK(texture->height() == 2);
    CHECK(texture->format() == PixelFormat::RGBA8);
}

TEST_CASE("the pixels handed over are the pixels the driver holds",
          "[render][texture][gpu]")
{
    gl_context();

    const auto texture = Texture::from_pixels(bytes_of(QUAD_PIXELS), 2, 2, PixelFormat::RGBA8);
    REQUIRE(texture.has_value());

    const std::vector<std::uint8_t> read = read_back(*texture);
    REQUIRE(read.size() == QUAD_PIXELS.size());

    trace("read back {} byte(s); first texel is ({}, {}, {}, {})", read.size(), read[0],
          read[1], read[2], read[3]);

    CHECK(std::ranges::equal(read, QUAD_PIXELS));
}

TEST_CASE("a row whose length is not a multiple of four survives the upload intact",
          "[render][texture][gpu]")
{
    gl_context();

    // This is the case GL_UNPACK_ALIGNMENT exists to break. A three-texel row of a
    // single-channel image is three bytes long; with the default alignment of four
    // the driver starts each row one byte late, and the image comes back sheared by
    // a texel per row. Ascending values make that shear unmistakable.
    SECTION("one channel, width three")
    {
        constexpr std::array<std::uint8_t, 9> pixels = {
            10, 20, 30,
            40, 50, 60,
            70, 80, 90,
        };

        const auto texture = Texture::from_pixels(bytes_of(pixels), 3, 3, PixelFormat::R8);
        REQUIRE(texture.has_value());

        const std::vector<std::uint8_t> read = read_back(*texture);

        trace("R8 3x3 read back as {} {} {} / {} {} {} / {} {} {}", read[0], read[1], read[2],
              read[3], read[4], read[5], read[6], read[7], read[8]);

        CHECK(std::ranges::equal(read, pixels));
    }

    SECTION("three channels, width one")
    {
        constexpr std::array<std::uint8_t, 6> pixels = {
            255, 128, 64,
            32,  16,  8,
        };

        const auto texture = Texture::from_pixels(bytes_of(pixels), 1, 2, PixelFormat::RGB8);
        REQUIRE(texture.has_value());

        const std::vector<std::uint8_t> read = read_back(*texture);

        trace("RGB8 1x2 read back as ({}, {}, {}) / ({}, {}, {})", read[0], read[1], read[2],
              read[3], read[4], read[5]);

        CHECK(std::ranges::equal(read, pixels));
    }
}

TEST_CASE("a texture can be built straight from a decoded image",
          "[render][texture][gpu]")
{
    gl_context();

    std::vector<std::byte> generated(image_size_in_bytes(4, 2, PixelFormat::RGBA8));
    for (std::size_t i = 0; i < generated.size(); ++i)
    {
        generated[i] = static_cast<std::byte>(i);
    }

    const auto image = Image::from_pixels(generated, 4, 2, PixelFormat::RGBA8);
    REQUIRE(image.has_value());

    const auto texture = Texture::from_image(*image);
    REQUIRE(texture.has_value());

    trace("image {}x{} became texture {}", image->width(), image->height(), texture->id());

    CHECK(texture->width() == image->width());
    CHECK(texture->height() == image->height());
    CHECK(texture->format() == image->format());

    const std::vector<std::uint8_t> read = read_back(*texture);
    CHECK(std::ranges::equal(bytes_of(read), image->pixels()));
}

TEST_CASE("a store can be allocated first and written afterwards",
          "[render][texture][gpu]")
{
    gl_context();

    auto texture = Texture::storage(4, 4, PixelFormat::R8);
    REQUIRE(texture.has_value());

    trace("allocated an empty {}x{} {} store as texture {}", texture->width(),
          texture->height(), to_string(texture->format()), texture->id());

    // A 2x2 patch written into the middle of a 4x4 image. Its position is what is
    // being checked: an update that ignored the offset would still write four
    // plausible-looking texels, just in the wrong corner.
    constexpr std::array<std::uint8_t, 4> patch = {11, 22, 33, 44};
    texture->update(bytes_of(patch), 1, 1, 2, 2);

    const std::vector<std::uint8_t> read = read_back(*texture);
    REQUIRE(read.size() == 16);

    trace_step("the patch landed at (1, 1) and nowhere else");

    CHECK(read[1 * 4 + 1] == 11);
    CHECK(read[1 * 4 + 2] == 22);
    CHECK(read[2 * 4 + 1] == 33);
    CHECK(read[2 * 4 + 2] == 44);

    // The corners are outside the patch. Their value is whatever the driver left
    // in an unfilled store, so the assertion is only that the patch did not reach
    // them — which the distinctive values above make checkable.
    CHECK(read[0] != 11);
    CHECK(read[15] != 44);
}

TEST_CASE("an update that would run past an edge is refused rather than clipped",
          "[render][texture][gpu]")
{
    gl_context();

    auto texture = Texture::storage(4, 4, PixelFormat::R8);
    REQUIRE(texture.has_value());

    constexpr std::array<std::uint8_t, 4> patch = {1, 2, 3, 4};

    SECTION("the rectangle starts inside but ends outside")
    {
        const LogCaptureGuard capture;
        texture->update(bytes_of(patch), 3, 3, 2, 2);

        CHECK(capture.count(Level::ERROR) == 1);
        CHECK(capture.contains("does not fit"));
    }

    SECTION("the offset alone is already past the edge")
    {
        const LogCaptureGuard capture;
        texture->update(bytes_of(patch), 9, 0, 2, 2);

        CHECK(capture.count(Level::ERROR) == 1);
    }

    SECTION("the buffer does not hold as many pixels as the rectangle asks for")
    {
        const LogCaptureGuard capture;
        texture->update(bytes_of(patch), 0, 0, 3, 3);

        CHECK(capture.count(Level::ERROR) == 1);
        CHECK(capture.contains("needs exactly"));
    }

    SECTION("an empty rectangle is not an error, it is simply nothing to do")
    {
        const LogCaptureGuard capture;
        texture->update({}, 0, 0, 0, 0);

        CHECK(capture.size() == 0);
    }
}

TEST_CASE("creation refuses a description that does not describe an image",
          "[render][texture][gpu]")
{
    gl_context();

    SECTION("a dimension of zero")
    {
        const auto texture = Texture::from_pixels(bytes_of(QUAD_PIXELS), 0, 2,
                                                  PixelFormat::RGBA8);
        REQUIRE_FALSE(texture.has_value());

        trace("zero width: {}", texture.error());
        CHECK(texture.error().code == ErrorCode::INVALID_FORMAT);
    }

    SECTION("a buffer that does not match the dimensions")
    {
        const auto texture = Texture::from_pixels(bytes_of(QUAD_PIXELS), 3, 3,
                                                  PixelFormat::RGBA8);
        REQUIRE_FALSE(texture.has_value());

        trace("size mismatch: {}", texture.error());
        CHECK(texture.error().code == ErrorCode::INVALID_FORMAT);
    }

    SECTION("a size beyond what the driver accepts")
    {
        const std::uint32_t oversized = Texture::maximum_size() + 1;
        trace("this driver's limit is {} per dimension", Texture::maximum_size());

        const auto texture = Texture::storage(oversized, 1, PixelFormat::RGBA8);
        REQUIRE_FALSE(texture.has_value());

        trace("oversized: {}", texture.error());
        CHECK(texture.error().code == ErrorCode::INVALID_FORMAT);
    }
}

TEST_CASE("binding puts the texture on the unit it was asked for",
          "[render][texture][gpu]")
{
    gl_context();

    const auto texture = Texture::from_pixels(bytes_of(QUAD_PIXELS), 2, 2, PixelFormat::RGBA8);
    REQUIRE(texture.has_value());

    SECTION("a unit the driver has")
    {
        texture->bind(3);

        trace("unit 3 holds texture {}, unit 0 holds {}", binding_of(3), binding_of(0));

        CHECK(binding_of(3) == static_cast<GLint>(texture->id()));
        CHECK(binding_of(0) != static_cast<GLint>(texture->id()));

        Texture::unbind(3);
        CHECK(binding_of(3) == 0);
    }

    SECTION("a unit the driver does not have is reported and ignored")
    {
        const unsigned int missing = Texture::maximum_units() + 1;

        const LogCaptureGuard capture;
        texture->bind(missing);

        CHECK(capture.count(Level::ERROR) == 1);
        CHECK(binding_of(0) != static_cast<GLint>(texture->id()));
    }
}

TEST_CASE("moving a texture transfers the name and leaves nothing to delete twice",
          "[render][texture][gpu]")
{
    gl_context();

    auto original = Texture::from_pixels(bytes_of(QUAD_PIXELS), 2, 2, PixelFormat::RGBA8);
    REQUIRE(original.has_value());

    const unsigned int name = original->id();

    {
        const Texture moved = std::move(*original);

        trace("name {} moved out; the source now reports {}", moved.id(), original->id());

        CHECK(moved.id() == name);
        CHECK(moved.width() == 2);
        CHECK(original->id() == 0);
        CHECK(glIsTexture(name) == GL_TRUE);
    }

    // The moved-to texture went out of scope and deleted the name; destroying the
    // moved-from one afterwards must not delete it a second time.
    CHECK(glIsTexture(name) == GL_FALSE);
}

TEST_CASE("a texture samples as the image it was given, the right way up",
          "[render][texture][gpu]")
{
    gl_context();

    // The end-to-end check, and the only one here that would catch an incomplete
    // texture: GL's default minifying filter needs a mipmap chain, and a texture
    // without one samples as opaque black with nothing reported anywhere. Every
    // assertion below would then read (0, 0, 0).
    constexpr int TARGET_SIZE = 64;

    constexpr const char* VERTEX_SHADER = R"(#version 330 core
layout (location = 0) in vec2 in_position;
layout (location = 1) in vec2 in_texture_coordinate;

out vec2 texture_coordinate;

void main()
{
    texture_coordinate = in_texture_coordinate;
    gl_Position = vec4(in_position, 0.0, 1.0);
}
)";

    constexpr const char* FRAGMENT_SHADER = R"(#version 330 core
in vec2 texture_coordinate;

uniform sampler2D source;

out vec4 fragment_color;

void main()
{
    fragment_color = texture(source, texture_coordinate);
}
)";

    // A quad covering the whole target, with texture coordinates running from
    // (0, 0) at the bottom-left corner to (1, 1) at the top-right one.
    constexpr std::array<float, 24> VERTICES = {
        // position      texture coordinate
        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f, -1.0f,    1.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,

        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,
        -1.0f,  1.0f,    0.0f, 1.0f,
    };

    auto shader = Shader::create("test.textured", VERTEX_SHADER, FRAGMENT_SHADER);
    REQUIRE(shader.has_value());

    // NEAREST so that each quadrant of the target is exactly one texel, with no
    // blending across the boundary to reason about.
    const auto texture = Texture::from_pixels(
        bytes_of(QUAD_PIXELS), 2, 2, PixelFormat::RGBA8,
        TextureConfig{.minify_filter = TextureFilter::NEAREST,
                      .magnify_filter = TextureFilter::NEAREST});
    REQUIRE(texture.has_value());

    const Buffer vertices = Buffer::vertex(VERTICES);

    VertexArray array;
    array.attach(vertices, VertexLayout{
                               .attributes = {
                                   {.type = AttributeType::FLOAT, .component_count = 2},
                                   {.type = AttributeType::FLOAT, .component_count = 2},
                               },
                           });

    GLint previous_viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, previous_viewport);

    GLuint color_buffer = 0;
    glGenRenderbuffers(1, &color_buffer);
    glBindRenderbuffer(GL_RENDERBUFFER, color_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, TARGET_SIZE, TARGET_SIZE);

    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                              color_buffer);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    cpen::render::set_viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
    cpen::render::clear(glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});

    shader->bind();
    shader->set_uniform("source", 0);
    texture->bind(0);

    cpen::render::draw_arrays(array, Primitive::TRIANGLES, 6);

    const auto read_pixel = [](const int x, const int y) {
        std::array<std::uint8_t, 4> color{};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
        return color;
    };

    const std::array<std::uint8_t, 4> bottom_left = read_pixel(1, 1);
    const std::array<std::uint8_t, 4> bottom_right = read_pixel(TARGET_SIZE - 2, 1);
    const std::array<std::uint8_t, 4> top_left = read_pixel(1, TARGET_SIZE - 2);
    const std::array<std::uint8_t, 4> top_right = read_pixel(TARGET_SIZE - 2, TARGET_SIZE - 2);

    trace("corners: bottom-left ({}, {}, {}), bottom-right ({}, {}, {})", bottom_left[0],
          bottom_left[1], bottom_left[2], bottom_right[0], bottom_right[1], bottom_right[2]);
    trace("         top-left ({}, {}, {}), top-right ({}, {}, {})", top_left[0], top_left[1],
          top_left[2], top_right[0], top_right[1], top_right[2]);

    trace_step("the first row of pixels uploaded is the row at v = 0");

    // Red and green were uploaded first, so they are the row a texture coordinate
    // of v = 0 selects — the bottom of the quad. Blue and white follow above them.
    // This is the whole of the orientation convention Image documents, checked
    // rather than asserted in prose.
    CHECK(bottom_left[0] == 255);
    CHECK(bottom_left[1] == 0);

    CHECK(bottom_right[0] == 0);
    CHECK(bottom_right[1] == 255);

    CHECK(top_left[2] == 255);
    CHECK(top_left[0] == 0);

    CHECK(top_right[0] == 255);
    CHECK(top_right[1] == 255);
    CHECK(top_right[2] == 255);

    Texture::unbind(0);
    Shader::unbind();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteRenderbuffers(1, &color_buffer);
    glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2],
               previous_viewport[3]);
}
