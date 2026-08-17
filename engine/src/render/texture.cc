#include "cpen/render/texture.hh"

#include "cpen/core/log.hh"
#include "cpen/render/image.hh"

#include <glad/glad.h>

#include <type_traits>
#include <unordered_set>
#include <utility>

namespace cpen::render
{
    namespace
    {
        // The public header describes texture names as plain integers so that
        // consumers need no GL headers. These assertions are what make that
        // description true rather than merely usual.
        static_assert(std::is_same_v<GLuint, unsigned int>);
        static_assert(std::is_same_v<GLenum, unsigned int>);

        GLenum to_gl_internal_format(const PixelFormat format) noexcept
        {
            switch (format)
            {
                case PixelFormat::R8:    return GL_R8;
                case PixelFormat::RGB8:  return GL_RGB8;
                case PixelFormat::RGBA8: return GL_RGBA8;
            }
            return GL_RGBA8;
        }

        /// The layout of the pixels being handed over, as opposed to the format
        /// the driver is asked to store them in. The two are given separately in
        /// GL and are only incidentally related: the same GL_RGBA upload can feed
        /// a GL_RGBA8 or a GL_SRGB8_ALPHA8 store.
        GLenum to_gl_source_format(const PixelFormat format) noexcept
        {
            switch (format)
            {
                case PixelFormat::R8:    return GL_RED;
                case PixelFormat::RGB8:  return GL_RGB;
                case PixelFormat::RGBA8: return GL_RGBA;
            }
            return GL_RGBA;
        }

        GLint to_gl_filter(const TextureFilter filter) noexcept
        {
            switch (filter)
            {
                case TextureFilter::NEAREST: return GL_NEAREST;
                case TextureFilter::LINEAR:  return GL_LINEAR;
            }
            return GL_LINEAR;
        }

        GLint to_gl_wrap(const TextureWrap wrap) noexcept
        {
            switch (wrap)
            {
                case TextureWrap::CLAMP_TO_EDGE:   return GL_CLAMP_TO_EDGE;
                case TextureWrap::REPEAT:          return GL_REPEAT;
                case TextureWrap::MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
            }
            return GL_CLAMP_TO_EDGE;
        }

        /// Sets GL_UNPACK_ALIGNMENT to one for the duration of an upload and puts
        /// back whatever it was.
        ///
        /// GL assumes each row of an upload begins on a four-byte boundary. That
        /// holds for every RGBA8 image and for no one else: a three-channel image
        /// of odd width, or a single-channel one of width not divisible by four,
        /// has rows that do not, and the driver then reads each row from a few
        /// bytes past where it really starts. The picture arrives skewed into a
        /// diagonal — a symptom that looks like a decoder bug and is not one.
        ///
        /// Restored rather than merely set back to the default of four, because
        /// the context is shared: in the test suite one process runs every case
        /// through a single context, so leaving global state behind is how one
        /// test comes to fail because of another.
        class UnpackAlignmentGuard
        {
        public:
            UnpackAlignmentGuard()
            {
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &this->previous_alignment);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            }

            ~UnpackAlignmentGuard()
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, this->previous_alignment);
            }

            UnpackAlignmentGuard(const UnpackAlignmentGuard&) = delete;
            UnpackAlignmentGuard& operator=(const UnpackAlignmentGuard&) = delete;

        private:
            GLint previous_alignment = 4;
        };

        /// Queries a driver limit once and remembers it.
        ///
        /// Sound because the engine has exactly one GL context per process —
        /// platform::Context refuses to be constructed twice — so a limit read
        /// once cannot go stale. Worth caching because bind() consults the unit
        /// count, and glGetIntegerv on the render path is a synchronisation point
        /// on some drivers.
        GLint driver_limit(const GLenum name)
        {
            GLint value = 0;
            glGetIntegerv(name, &value);
            return value;
        }

        /// Complains about a texture unit the driver does not have, once per unit.
        ///
        /// Once rather than every time for the same reason Shader records its
        /// rejected uniform names: binding happens per draw, and an unconditional
        /// message would arrive sixty times a second.
        void report_invalid_unit(const unsigned int unit, const unsigned int available)
        {
            static std::unordered_set<unsigned int> reported;

            if (reported.insert(unit).second)
            {
                log::error(log::Category::RENDER,
                           "texture unit {} does not exist; this driver offers {}. "
                           "The bind is ignored",
                           unit, available);
            }
        }
    }

    Texture::Texture(const unsigned int texture, const std::uint32_t width,
                     const std::uint32_t height, const PixelFormat format) noexcept
        : handle(texture),
          pixel_width(width),
          pixel_height(height),
          pixel_format(format)
    {
    }

    Texture::~Texture()
    {
        this->destroy();
    }

    Texture::Texture(Texture&& other) noexcept
        : handle(std::exchange(other.handle, 0)),
          pixel_width(std::exchange(other.pixel_width, 0)),
          pixel_height(std::exchange(other.pixel_height, 0)),
          pixel_format(other.pixel_format)
    {
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other)
        {
            this->destroy();

            this->handle = std::exchange(other.handle, 0);
            this->pixel_width = std::exchange(other.pixel_width, 0);
            this->pixel_height = std::exchange(other.pixel_height, 0);
            this->pixel_format = other.pixel_format;
        }
        return *this;
    }

    void Texture::destroy() noexcept
    {
        // Deleting texture name 0 is defined as a no-op, so a moved-from Texture
        // needs no separate handling here.
        glDeleteTextures(1, &this->handle);
        this->handle = 0;
        this->pixel_width = 0;
        this->pixel_height = 0;
    }

    std::uint32_t Texture::maximum_size()
    {
        static const GLint limit = driver_limit(GL_MAX_TEXTURE_SIZE);
        return limit > 0 ? static_cast<std::uint32_t>(limit) : 0;
    }

    unsigned int Texture::maximum_units()
    {
        static const GLint limit = driver_limit(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
        return limit > 0 ? static_cast<unsigned int>(limit) : 0;
    }

    std::expected<Texture, core::Error> Texture::from_pixels(
        const std::span<const std::byte> pixels, const std::uint32_t width,
        const std::uint32_t height, const PixelFormat format, const TextureConfig& config)
    {
        return create(pixels.data(), pixels.size(), width, height, format, config);
    }

    std::expected<Texture, core::Error> Texture::from_image(const Image& image,
                                                            const TextureConfig& config)
    {
        return from_pixels(image.pixels(), image.width(), image.height(), image.format(),
                           config);
    }

    std::expected<Texture, core::Error> Texture::storage(const std::uint32_t width,
                                                         const std::uint32_t height,
                                                         const PixelFormat format,
                                                         const TextureConfig& config)
    {
        // A null pointer is glTexImage2D's own way of asking for the store
        // without an upload, so the size check below has nothing to compare and
        // is skipped by passing the size the store will have.
        return create(nullptr, image_size_in_bytes(width, height, format), width, height,
                      format, config);
    }

    std::expected<Texture, core::Error> Texture::create(const std::byte* const pixels,
                                                        const std::size_t size_in_bytes,
                                                        const std::uint32_t width,
                                                        const std::uint32_t height,
                                                        const PixelFormat format,
                                                        const TextureConfig& config)
    {
        if (width == 0 || height == 0)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT, "a texture of {}x{} has no pixels",
                width, height));
        }

        const std::uint32_t limit = maximum_size();
        if (width > limit || height > limit)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT,
                "a texture of {}x{} exceeds this driver's limit of {} per dimension",
                width, height, limit));
        }

        const std::size_t expected_size = image_size_in_bytes(width, height, format);
        if (size_in_bytes != expected_size)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT,
                "a {}x{} {} texture needs exactly {} byte(s) of pixel data, got {}",
                width, height, to_string(format), expected_size, size_in_bytes));
        }

        GLuint handle = 0;
        glGenTextures(1, &handle);

        // Bound through whichever unit is active, and left bound to nothing
        // afterwards: the same convention Buffer follows for GL_ARRAY_BUFFER.
        // Creating a texture therefore clobbers the active unit's binding, which
        // is why a renderer binds its textures after creating them, never before.
        glBindTexture(GL_TEXTURE_2D, handle);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        to_gl_filter(config.minify_filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        to_gl_filter(config.magnify_filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        to_gl_wrap(config.wrap_horizontal));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        to_gl_wrap(config.wrap_vertical));

        {
            const UnpackAlignmentGuard alignment;

            glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(to_gl_internal_format(format)),
                         static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                         to_gl_source_format(format), GL_UNSIGNED_BYTE, pixels);
        }

        glBindTexture(GL_TEXTURE_2D, 0);

        log::debug(log::Category::RENDER,
                   "texture {} created, {}x{} {}, {} filtering, {} byte(s){}",
                   handle, width, height, to_string(format),
                   to_string(config.magnify_filter), expected_size,
                   pixels != nullptr ? "" : " (store only, not filled)");

        return Texture{handle, width, height, format};
    }

    void Texture::bind(const unsigned int unit) const
    {
        const unsigned int available = maximum_units();
        if (unit >= available)
        {
            report_invalid_unit(unit, available);
            return;
        }

        // The unit constants are consecutive, which the specification guarantees
        // precisely so that they can be indexed like this.
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, this->handle);
    }

    void Texture::unbind(const unsigned int unit)
    {
        const unsigned int available = maximum_units();
        if (unit >= available)
        {
            report_invalid_unit(unit, available);
            return;
        }

        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void Texture::update(const std::span<const std::byte> pixels, const std::uint32_t x,
                         const std::uint32_t y, const std::uint32_t width,
                         const std::uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return;
        }

        // Subtraction rather than addition on the left: x + width could wrap round
        // on a caller's arithmetic mistake and let an out-of-bounds write through.
        if (x > this->pixel_width || width > this->pixel_width - x ||
            y > this->pixel_height || height > this->pixel_height - y)
        {
            log::error(log::Category::RENDER,
                       "texture {}: a {}x{} update at ({}, {}) does not fit a {}x{} image; "
                       "the write is ignored",
                       this->handle, width, height, x, y, this->pixel_width,
                       this->pixel_height);
            return;
        }

        const std::size_t expected_size = image_size_in_bytes(width, height, this->pixel_format);
        if (pixels.size() != expected_size)
        {
            log::error(log::Category::RENDER,
                       "texture {}: a {}x{} {} update needs exactly {} byte(s), got {}; "
                       "the write is ignored",
                       this->handle, width, height, to_string(this->pixel_format),
                       expected_size, pixels.size());
            return;
        }

        glBindTexture(GL_TEXTURE_2D, this->handle);

        {
            const UnpackAlignmentGuard alignment;

            glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(x), static_cast<GLint>(y),
                            static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                            to_gl_source_format(this->pixel_format), GL_UNSIGNED_BYTE,
                            pixels.data());
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
