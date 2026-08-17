#ifndef CPEN_RENDER_TEXTURE_HH
#define CPEN_RENDER_TEXTURE_HH

#include "cpen/core/error.hh"
#include "cpen/render/pixel_format.hh"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace cpen::render
{
    class Image;

    /// How a texel is chosen when one texel does not land on one pixel.
    enum class TextureFilter : std::uint8_t
    {
        /// The nearest texel. What pixel art needs: anything else turns a crisp
        /// edge into a smear as soon as the sprite is not at an exact scale.
        NEAREST,
        /// A weighted average of the four surrounding texels. What a photographic
        /// background or a scaled glyph atlas needs.
        LINEAR,
    };

    /// What is sampled outside the 0..1 range of texture coordinates.
    enum class TextureWrap : std::uint8_t
    {
        /// The edge texel is repeated outwards. The right default for a sprite in
        /// an atlas: it is the only mode that cannot bleed a neighbouring sprite
        /// into the current one when a coordinate lands slightly outside.
        CLAMP_TO_EDGE,
        REPEAT,
        MIRRORED_REPEAT,
    };

    constexpr std::string_view to_string(const TextureFilter filter) noexcept
    {
        switch (filter)
        {
            case TextureFilter::NEAREST: return "nearest";
            case TextureFilter::LINEAR:  return "linear";
        }
        return "unknown";
    }

    constexpr std::string_view to_string(const TextureWrap wrap) noexcept
    {
        switch (wrap)
        {
            case TextureWrap::CLAMP_TO_EDGE:   return "clamp to edge";
            case TextureWrap::REPEAT:          return "repeat";
            case TextureWrap::MIRRORED_REPEAT: return "mirrored repeat";
        }
        return "unknown";
    }

    /// The sampling parameters a texture is created with.
    ///
    /// Fixed at creation, with no setters. GL would allow them to be changed at
    /// any time, but nothing in the engine yet needs to — and a parameter that
    /// can change under a renderer holding the texture by const reference is a
    /// frame-order bug waiting to be written.
    struct TextureConfig
    {
        TextureFilter minify_filter = TextureFilter::LINEAR;
        TextureFilter magnify_filter = TextureFilter::LINEAR;
        TextureWrap wrap_horizontal = TextureWrap::CLAMP_TO_EDGE;
        TextureWrap wrap_vertical = TextureWrap::CLAMP_TO_EDGE;
    };

    /// A GL 2D texture object: the image the driver holds, plus the size and
    /// format it was created with.
    ///
    /// Ownership is move-only, the same shape as Shader, Buffer and VertexArray,
    /// and carries the same requirement not to outlive the GL context.
    ///
    /// One property worth knowing even though this class makes it unreachable:
    /// GL's default minifying filter is GL_NEAREST_MIPMAP_LINEAR, and a texture
    /// with that filter and no mipmap chain is *incomplete* — it samples as
    /// opaque black with no error reported anywhere. Creation here always writes
    /// both filters, and TextureFilter offers no mipmapped value, so the classic
    /// black-texture symptom cannot arise. Mipmaps are absent because a 2D game
    /// drawing at or near a 1:1 scale gains nothing from them but a third more
    /// memory; they arrive with the first thing that minifies far enough to want
    /// them.
    class Texture
    {
    public:
        /// Uploads `pixels`, which must be exactly
        /// width * height * pixel_size(format) bytes, tightly packed and in
        /// top-row-first order.
        static std::expected<Texture, core::Error> from_pixels(
            std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height,
            PixelFormat format, const TextureConfig& config = {});

        /// Uploads a decoded image. A forwarder to from_pixels() — the dependency
        /// runs this way round only, so that Image stays free of GL and Texture
        /// stays free of any decoder.
        static std::expected<Texture, core::Error> from_image(
            const Image& image, const TextureConfig& config = {});

        /// Allocates the store without filling it, for contents written later
        /// with update(). Sampling it before it is written is not an error in GL;
        /// it reads whatever the driver left behind. This is what a glyph atlas
        /// is built on: allocate once, write each glyph as it is rasterised.
        static std::expected<Texture, core::Error> storage(
            std::uint32_t width, std::uint32_t height, PixelFormat format,
            const TextureConfig& config = {});

        ~Texture();

        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&& other) noexcept;

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        /// Binds this texture to a texture unit, so that a sampler uniform set to
        /// `unit` reads from it.
        ///
        /// The unit is passed explicitly rather than defaulting silently to
        /// whichever one happens to be active: unlike every other binding in this
        /// layer, the active unit is a second piece of global state that a caller
        /// elsewhere may have moved. A unit the driver does not have is reported
        /// and ignored.
        ///
        /// Const for the same reason Shader::bind() is: binding changes GL's
        /// global state, not this texture's contents.
        void bind(unsigned int unit = 0) const;
        static void unbind(unsigned int unit = 0);

        /// Overwrites a rectangle of the image, leaving its size and format
        /// unchanged. `pixels` must hold exactly width * height pixels of this
        /// texture's format.
        ///
        /// A write that would run past an edge is reported and dropped rather
        /// than clipped, on the same reasoning as Buffer::update(): a partially
        /// applied upload draws something plausible but wrong, which is harder to
        /// track down than a write that visibly did nothing.
        void update(std::span<const std::byte> pixels, std::uint32_t x, std::uint32_t y,
                    std::uint32_t width, std::uint32_t height);

        std::uint32_t width() const noexcept { return this->pixel_width; }
        std::uint32_t height() const noexcept { return this->pixel_height; }
        PixelFormat format() const noexcept { return this->pixel_format; }

        /// The raw texture name, for the few places that must talk to GL directly.
        /// GLuint is unsigned int on every platform the engine targets, which is
        /// what lets this header stay free of the GL headers.
        unsigned int id() const noexcept { return this->handle; }

        /// The largest width or height this driver accepts, as reported by GL.
        /// Requires a current context.
        static std::uint32_t maximum_size();

        /// The number of texture units this driver offers to the fragment stage.
        /// Requires a current context.
        static unsigned int maximum_units();

    private:
        /// The one real constructor. A null `pixels` allocates the store without
        /// filling it, which is glTexImage2D's own convention.
        Texture(unsigned int texture, std::uint32_t width, std::uint32_t height,
                PixelFormat format) noexcept;

        /// Validates the dimensions and the buffer size shared by all three
        /// factories, then creates and uploads.
        static std::expected<Texture, core::Error> create(
            const std::byte* pixels, std::size_t size_in_bytes, std::uint32_t width,
            std::uint32_t height, PixelFormat format, const TextureConfig& config);

        void destroy() noexcept;

        unsigned int handle = 0;
        std::uint32_t pixel_width = 0;
        std::uint32_t pixel_height = 0;
        PixelFormat pixel_format = PixelFormat::RGBA8;
    };
}

#endif //CPEN_RENDER_TEXTURE_HH
