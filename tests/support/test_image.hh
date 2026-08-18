#ifndef CPEN_TESTS_SUPPORT_TEST_IMAGE_HH
#define CPEN_TESTS_SUPPORT_TEST_IMAGE_HH

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cpen::test
{
    /// A real, valid, 2x2 RGBA PNG — the smallest thing the asset layer can be
    /// asked to load that exercises the decoder for real.
    ///
    /// Written out as bytes rather than committed as a file for the same reason
    /// the temporary directory exists: a case that needs the picture under a
    /// different name, in a different place, or absent, can have it, and nothing
    /// in the repository has to be opened in an image editor to know what it
    /// contains.
    ///
    /// Its four pixels, in row order from the top left:
    ///
    ///   red (220, 30, 30, 255)      green (30, 200, 30, 255)
    ///   blue (30, 30, 220, 255)     white at half alpha (255, 255, 255, 128)
    ///
    /// The last one is the interesting one: it is the pixel that tells a
    /// premultiplied picture from a straight one.
    inline std::span<const std::byte> tiny_png() noexcept
    {
        static constexpr std::array<std::uint8_t, 82> BYTES = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
            0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
            0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00,
            0x19, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xB8, 0x23, 0x27, 0xF7,
            0x5F, 0xEE, 0x84, 0xDC, 0x7F, 0x06, 0x39, 0xB9, 0x3B, 0xFF, 0x81, 0xA0,
            0x01, 0x00, 0x4B, 0xC4, 0x09, 0xAF, 0xFB, 0x40, 0xEF, 0x73, 0x00, 0x00,
            0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
        };

        return std::as_bytes(std::span{BYTES});
    }

    /// The same bytes as text, for writing straight into a file.
    inline std::string_view tiny_png_text() noexcept
    {
        const std::span<const std::byte> bytes = tiny_png();
        return std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
}

#endif //CPEN_TESTS_SUPPORT_TEST_IMAGE_HH
