#include "cpen/render/image.hh"

#include "cpen/core/log.hh"

#include <stb_image.h>

#include <cstring>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace cpen::render
{
    namespace
    {
        /// Releases a buffer allocated by stb_image.
        ///
        /// stb allocates the decoded pixels with its own allocator and hands back
        /// a raw pointer, so a guard is needed for the window between the decode
        /// and the copy into the Image: the vector allocation in between can throw.
        struct DecodedPixelsDeleter
        {
            void operator()(stbi_uc* const pixels) const noexcept
            {
                stbi_image_free(pixels);
            }
        };

        using DecodedPixels = std::unique_ptr<stbi_uc, DecodedPixelsDeleter>;

        /// The reason stb recorded for the last failed decode.
        ///
        /// Not thread-safe in stb — the reason lives in a global — but decoding
        /// happens on the loading path, and the alternative is an error message
        /// that says only that something went wrong.
        std::string_view last_decode_failure()
        {
            const char* const reason = stbi_failure_reason();
            return reason != nullptr ? reason : "no reason given";
        }
    }

    Image::Image(std::vector<std::byte> pixels, const std::uint32_t width,
                 const std::uint32_t height, const PixelFormat format) noexcept
        : data(std::move(pixels)),
          pixel_width(width),
          pixel_height(height),
          pixel_format(format)
    {
    }

    std::expected<Image, core::Error> Image::from_file(const std::filesystem::path& path,
                                                       const PixelFormat format)
    {
        // Opened and read here rather than through stb's own stdio path, which
        // takes a narrow char const* and therefore cannot name a file whose path
        // is not representable in the active Windows code page. See stb_image.c.
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::FILE_NOT_FOUND, "cannot open image file '{}'", path.string()));
        }

        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size <= 0 || !file)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT, "image file '{}' is empty or unreadable",
                path.string()));
        }

        std::vector<std::byte> encoded(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(encoded.data()), size);

        if (file.gcount() != size)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT,
                "image file '{}': read {} of {} byte(s)", path.string(),
                static_cast<std::size_t>(file.gcount()), static_cast<std::size_t>(size)));
        }

        // The decoder's message says what was wrong with the data but not which
        // file it came from, which is the half a reader actually needs first.
        // That both halves compose into one message at all is the point of the
        // engine having a single error type rather than one per layer.
        return from_memory(encoded, format).transform_error([&path](core::Error error) {
            error.message = std::format("image file '{}': {}", path.string(), error.message);
            return error;
        });
    }

    std::expected<Image, core::Error> Image::from_memory(const std::span<const std::byte> encoded,
                                                         const PixelFormat format)
    {
        if (encoded.empty())
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT, "cannot decode an empty buffer"));
        }

        // stb takes the length as an int, so an oversized buffer has to be
        // rejected here rather than silently truncated by the conversion.
        if (encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT,
                "encoded image of {} byte(s) exceeds the {} byte(s) the decoder accepts",
                encoded.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
        }

        int width = 0;
        int height = 0;
        int channels_in_file = 0;

        const DecodedPixels decoded{stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(encoded.data()), static_cast<int>(encoded.size()),
            &width, &height, &channels_in_file,
            static_cast<int>(channel_count(format)))};

        if (decoded == nullptr)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT, "decode failed ({})", last_decode_failure()));
        }

        if (width <= 0 || height <= 0)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT, "decoded to {}x{}, which has no pixels",
                width, height));
        }

        const auto decoded_width = static_cast<std::uint32_t>(width);
        const auto decoded_height = static_cast<std::uint32_t>(height);
        const std::size_t size = image_size_in_bytes(decoded_width, decoded_height, format);

        // One copy out of stb's allocation into a std::vector. It could be
        // avoided by carrying stb's pointer and its deleter around instead, but
        // then a generated image and a decoded one would be different types, and
        // the copy costs a memcpy once per asset load.
        std::vector<std::byte> pixels(size);
        std::memcpy(pixels.data(), decoded.get(), size);

        log::debug(log::Category::RENDER, "decoded a {}x{} image as {} ({} channel(s) in file)",
                   decoded_width, decoded_height, to_string(format), channels_in_file);

        return Image{std::move(pixels), decoded_width, decoded_height, format};
    }

    std::expected<Image, core::Error> Image::from_pixels(std::vector<std::byte> pixels,
                                                         const std::uint32_t width,
                                                         const std::uint32_t height,
                                                         const PixelFormat format)
    {
        if (width == 0 || height == 0)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT, "an image of {}x{} has no pixels",
                width, height));
        }

        const std::size_t expected_size = image_size_in_bytes(width, height, format);
        if (pixels.size() != expected_size)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT,
                "a {}x{} {} image needs exactly {} byte(s), got {}",
                width, height, to_string(format), expected_size, pixels.size()));
        }

        return Image{std::move(pixels), width, height, format};
    }
}
