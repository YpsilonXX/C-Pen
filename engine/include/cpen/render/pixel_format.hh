#ifndef CPEN_RENDER_PIXEL_FORMAT_HH
#define CPEN_RENDER_PIXEL_FORMAT_HH

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cpen::render
{
    /// How many channels one pixel has, and therefore how the bytes of a pixel
    /// buffer are to be read.
    ///
    /// Its own header because it is the one thing Image and Texture must agree
    /// on. Neither includes the other for it: an Image never touches GL, and a
    /// Texture built from generated pixels never needs a decoder.
    ///
    /// Every format is eight bits per channel and unsigned. Wider ones are not
    /// omitted for lack of use — a 2D engine has no need for a float texture
    /// until it grows HDR or shadow maps, and adding one costs a new enumerator
    /// then rather than a wrong abstraction now.
    enum class PixelFormat : std::uint8_t
    {
        /// One channel. Sampled as (r, 0, 0, 1) in GLSL, so a shader reading one
        /// must take .r deliberately rather than expect a grey. This is the form
        /// a FreeType glyph atlas takes, at a quarter of RGBA8's footprint.
        R8,
        /// Three channels, no alpha. Worth choosing for a full-screen background,
        /// where the fourth channel is a wasted two megabytes at 1920x1080.
        RGB8,
        /// Four channels. The default everywhere else: anything composited over
        /// something else needs the alpha, and the row length is a multiple of
        /// four whatever the width, which is the alignment GL expects by default.
        RGBA8,
    };

    /// The number of channels in one pixel of `format`.
    constexpr std::size_t channel_count(const PixelFormat format) noexcept
    {
        switch (format)
        {
            case PixelFormat::R8:    return 1;
            case PixelFormat::RGB8:  return 3;
            case PixelFormat::RGBA8: return 4;
        }
        return 0;
    }

    /// The size in bytes of one pixel of `format`.
    constexpr std::size_t pixel_size(const PixelFormat format) noexcept
    {
        // Every channel is one byte, which is what makes this the channel count.
        return channel_count(format);
    }

    /// The size in bytes of a tightly packed `width` x `height` image.
    ///
    /// Computed in std::size_t throughout: the product of two 32-bit dimensions
    /// and a channel count overflows 32 bits well below the largest texture a
    /// driver will accept.
    constexpr std::size_t image_size_in_bytes(const std::uint32_t width,
                                              const std::uint32_t height,
                                              const PixelFormat format) noexcept
    {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
               pixel_size(format);
    }

    constexpr std::string_view to_string(const PixelFormat format) noexcept
    {
        switch (format)
        {
            case PixelFormat::R8:    return "R8";
            case PixelFormat::RGB8:  return "RGB8";
            case PixelFormat::RGBA8: return "RGBA8";
        }
        return "unknown";
    }
}

#endif //CPEN_RENDER_PIXEL_FORMAT_HH
