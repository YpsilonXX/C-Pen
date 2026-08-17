#ifndef CPEN_RENDER_IMAGE_HH
#define CPEN_RENDER_IMAGE_HH

#include "cpen/core/error.hh"
#include "cpen/render/pixel_format.hh"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace cpen::render
{
    /// Decoded pixels in main memory: the data a Texture is built from, before
    /// any of it reaches the driver.
    ///
    /// Separate from Texture on purpose, and for the same reason Shader takes
    /// source text rather than a file path: the render layer is the part that
    /// talks to GL, and reading and decoding files is a different job that
    /// happens to feed it. Keeping the two apart means Texture never opens a
    /// file, Image never needs a GL context — so its tests run in the ordinary
    /// suite rather than the GPU one — and the asset layer of a later phase can
    /// take ownership of Image without Texture noticing.
    ///
    /// Row order is the file's own: the first row of pixels() is the top row of
    /// the picture. Nothing is flipped on the way in, even though GL's texture
    /// origin is the bottom-left corner, because the correction belongs to
    /// whoever assigns texture coordinates — a sprite maps v = 0 to the top row
    /// and the image is upright with no copy made and no global decoder flag set.
    ///
    /// Copyable, unlike the GL-owning types: this is a value, not a handle. The
    /// copy is a real one, so pass it by const reference on hot paths.
    class Image
    {
    public:
        /// Reads and decodes an image file.
        ///
        /// `format` is what the pixels are converted to, not an assertion about
        /// the file: a three-channel PNG requested as RGBA8 arrives with its
        /// alpha filled with 255, and any file requested as R8 is converted to
        /// luminance rather than having its red channel taken.
        static std::expected<Image, core::Error> from_file(
            const std::filesystem::path& path, PixelFormat format = PixelFormat::RGBA8);

        /// Decodes an image already held in memory — an archive entry, a
        /// downloaded asset, or bytes compiled into the binary.
        static std::expected<Image, core::Error> from_memory(
            std::span<const std::byte> encoded, PixelFormat format = PixelFormat::RGBA8);

        /// Wraps pixels that were generated rather than decoded.
        ///
        /// Takes the buffer by value and moves from it, so a generated image
        /// costs no copy. Fails if the buffer's size is not exactly
        /// width * height * pixel_size(format): a buffer that merely holds enough
        /// is far more often an arithmetic mistake than an intentional one.
        static std::expected<Image, core::Error> from_pixels(
            std::vector<std::byte> pixels, std::uint32_t width, std::uint32_t height,
            PixelFormat format);

        std::span<const std::byte> pixels() const noexcept { return this->data; }

        std::uint32_t width() const noexcept { return this->pixel_width; }
        std::uint32_t height() const noexcept { return this->pixel_height; }
        PixelFormat format() const noexcept { return this->pixel_format; }

        std::size_t size_in_bytes() const noexcept { return this->data.size(); }

        /// The distance in bytes between the start of one row and the next.
        /// Rows are tightly packed, with no padding between them.
        std::size_t row_size_in_bytes() const noexcept
        {
            return static_cast<std::size_t>(this->pixel_width) * pixel_size(this->pixel_format);
        }

    private:
        Image(std::vector<std::byte> pixels, std::uint32_t width, std::uint32_t height,
              PixelFormat format) noexcept;

        std::vector<std::byte> data;
        std::uint32_t pixel_width = 0;
        std::uint32_t pixel_height = 0;
        PixelFormat pixel_format = PixelFormat::RGBA8;
    };
}

#endif //CPEN_RENDER_IMAGE_HH
