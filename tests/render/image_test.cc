#include <catch2/catch_test_macros.hpp>

#include "cpen/core/error.hh"
#include "cpen/render/image.hh"
#include "support/trace.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using cpen::core::ErrorCode;
using cpen::render::Image;
using cpen::render::PixelFormat;
using cpen::test::trace;
using cpen::test::trace_step;

// Image decodes into main memory and never touches GL, which is the whole reason
// it is a type of its own — so its tests belong in the suite that runs on a
// machine with no driver and no display.

namespace
{
    /// A 2x2 truecolour PNG: red and green on the top row, blue and white on the
    /// bottom one.
    ///
    /// Written out as bytes rather than shipped as a file so that the suite stays
    /// free of binary fixtures and of any assumption about the working directory
    /// it is run from. Seventy-five bytes is the whole of a minimal PNG: an
    /// eight-byte signature and three chunks.
    ///
    /// The asymmetry between the rows is deliberate — it is what distinguishes a
    /// correctly ordered decode from a vertically flipped one.
    constexpr std::array<std::byte, 75> TEST_PNG = {
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0d},
        std::byte{0x49}, std::byte{0x48}, std::byte{0x44}, std::byte{0x52},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x08}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0xfd}, std::byte{0xd4}, std::byte{0x9a},
        std::byte{0x73}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x12}, std::byte{0x49}, std::byte{0x44}, std::byte{0x41},
        std::byte{0x54}, std::byte{0x78}, std::byte{0xda}, std::byte{0x63},
        std::byte{0xf8}, std::byte{0xcf}, std::byte{0xc0}, std::byte{0xc0},
        std::byte{0x00}, std::byte{0xc2}, std::byte{0x0c}, std::byte{0xff},
        std::byte{0x81}, std::byte{0x00}, std::byte{0x00}, std::byte{0x1f},
        std::byte{0xee}, std::byte{0x05}, std::byte{0xfb}, std::byte{0xf1},
        std::byte{0xab}, std::byte{0xba}, std::byte{0x77}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x49},
        std::byte{0x45}, std::byte{0x4e}, std::byte{0x44}, std::byte{0xae},
        std::byte{0x42}, std::byte{0x60}, std::byte{0x82},
    };

    /// The value of one channel, as a number rather than as a std::byte, so that
    /// a failing CHECK prints 255 instead of an unprintable character.
    unsigned int channel(const Image& image, const std::size_t index)
    {
        return static_cast<unsigned int>(image.pixels()[index]);
    }

    /// Writes `contents` to a uniquely named file under the system temporary
    /// directory and removes it again when the guard goes out of scope.
    class TemporaryFile
    {
    public:
        explicit TemporaryFile(const std::span<const std::byte> contents)
            : file_path(std::filesystem::temp_directory_path() /
                        std::format("cpen_image_test_{}.png", ++counter))
        {
            std::ofstream file(this->file_path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(contents.data()),
                       static_cast<std::streamsize>(contents.size()));
        }

        ~TemporaryFile()
        {
            std::error_code ignored;
            std::filesystem::remove(this->file_path, ignored);
        }

        TemporaryFile(const TemporaryFile&) = delete;
        TemporaryFile& operator=(const TemporaryFile&) = delete;

        const std::filesystem::path& path() const noexcept { return this->file_path; }

    private:
        static inline unsigned int counter = 0;

        std::filesystem::path file_path;
    };
}

TEST_CASE("a PNG decodes to the pixels it was written from", "[render][image]")
{
    const auto image = Image::from_memory(TEST_PNG, PixelFormat::RGBA8);
    REQUIRE(image.has_value());

    trace("decoded {}x{} as {}, {} byte(s)", image->width(), image->height(),
          to_string(image->format()), image->size_in_bytes());

    CHECK(image->width() == 2);
    CHECK(image->height() == 2);
    CHECK(image->format() == PixelFormat::RGBA8);
    CHECK(image->size_in_bytes() == 2 * 2 * 4);
    CHECK(image->row_size_in_bytes() == 2 * 4);

    trace_step("the first row is the top row of the picture, not the bottom one");

    // Red, then green. Were the rows flipped, this would read blue.
    CHECK(channel(*image, 0) == 255);
    CHECK(channel(*image, 1) == 0);
    CHECK(channel(*image, 2) == 0);

    CHECK(channel(*image, 4) == 0);
    CHECK(channel(*image, 5) == 255);
    CHECK(channel(*image, 6) == 0);

    trace_step("the second row follows immediately, with no padding between rows");

    // Blue, then white, at byte 8: one row of two RGBA pixels behind them.
    CHECK(channel(*image, 8) == 0);
    CHECK(channel(*image, 10) == 255);

    CHECK(channel(*image, 12) == 255);
    CHECK(channel(*image, 13) == 255);
    CHECK(channel(*image, 14) == 255);
}

TEST_CASE("a file with no alpha gains an opaque one when decoded as RGBA8",
          "[render][image]")
{
    const auto image = Image::from_memory(TEST_PNG, PixelFormat::RGBA8);
    REQUIRE(image.has_value());

    trace("alpha of the four pixels: {}, {}, {}, {}", channel(*image, 3), channel(*image, 7),
          channel(*image, 11), channel(*image, 15));

    CHECK(channel(*image, 3) == 255);
    CHECK(channel(*image, 7) == 255);
    CHECK(channel(*image, 11) == 255);
    CHECK(channel(*image, 15) == 255);
}

TEST_CASE("the requested format decides the channel count, not the file",
          "[render][image]")
{
    SECTION("three channels, as the file itself holds")
    {
        const auto image = Image::from_memory(TEST_PNG, PixelFormat::RGB8);
        REQUIRE(image.has_value());

        trace("as RGB8: {} byte(s) for {} pixel(s)", image->size_in_bytes(), 2 * 2);
        CHECK(image->size_in_bytes() == 2 * 2 * 3);
        CHECK(channel(*image, 0) == 255);
        CHECK(channel(*image, 1) == 0);
    }

    SECTION("one channel, converted to luminance rather than truncated")
    {
        const auto image = Image::from_memory(TEST_PNG, PixelFormat::R8);
        REQUIRE(image.has_value());

        trace("as R8: {} byte(s), pixels {} {} {} {}", image->size_in_bytes(),
              channel(*image, 0), channel(*image, 1), channel(*image, 2), channel(*image, 3));

        CHECK(image->size_in_bytes() == 2 * 2);

        // White stays white under any luminance formula, which is what makes this
        // worth asserting; the exact value red collapses to is the decoder's own
        // business and is deliberately not checked.
        CHECK(channel(*image, 3) == 255);

        // Pure red is not white and not black, whatever the weights.
        CHECK(channel(*image, 0) > 0);
        CHECK(channel(*image, 0) < 255);
    }
}

TEST_CASE("decoding rejects what is not an image", "[render][image]")
{
    SECTION("an empty buffer")
    {
        const auto image = Image::from_memory({}, PixelFormat::RGBA8);
        REQUIRE_FALSE(image.has_value());

        trace("empty buffer: {}", image.error());
        CHECK(image.error().code == ErrorCode::INVALID_FORMAT);
    }

    SECTION("bytes that are not any known format")
    {
        constexpr std::array<std::byte, 8> garbage = {
            std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
            std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88},
        };

        const auto image = Image::from_memory(garbage, PixelFormat::RGBA8);
        REQUIRE_FALSE(image.has_value());

        trace("garbage: {}", image.error());
        CHECK(image.error().code == ErrorCode::INVALID_FORMAT);
    }

    SECTION("a truncated file, cut off inside its pixel data")
    {
        const auto image = Image::from_memory(std::span{TEST_PNG}.first(40), PixelFormat::RGBA8);
        REQUIRE_FALSE(image.has_value());

        trace("truncated: {}", image.error());
        CHECK(image.error().code == ErrorCode::INVALID_FORMAT);
    }
}

TEST_CASE("an image reads the same from a file as from memory", "[render][image]")
{
    const TemporaryFile file(TEST_PNG);
    trace("wrote the test PNG to {}", file.path().string());

    const auto from_disk = Image::from_file(file.path(), PixelFormat::RGBA8);
    REQUIRE(from_disk.has_value());

    const auto from_memory = Image::from_memory(TEST_PNG, PixelFormat::RGBA8);
    REQUIRE(from_memory.has_value());

    CHECK(from_disk->width() == from_memory->width());
    CHECK(from_disk->height() == from_memory->height());
    CHECK(std::ranges::equal(from_disk->pixels(), from_memory->pixels()));
}

TEST_CASE("a missing file is distinguished from a malformed one", "[render][image]")
{
    SECTION("the path does not exist")
    {
        const auto image = Image::from_file(
            std::filesystem::temp_directory_path() / "cpen_no_such_image_37642.png");
        REQUIRE_FALSE(image.has_value());

        trace("missing file: {}", image.error());
        CHECK(image.error().code == ErrorCode::FILE_NOT_FOUND);
    }

    SECTION("the file exists but holds no image")
    {
        constexpr std::array<std::byte, 4> garbage = {
            std::byte{'n'}, std::byte{'o'}, std::byte{'p'}, std::byte{'e'},
        };
        const TemporaryFile file(garbage);

        const auto image = Image::from_file(file.path());
        REQUIRE_FALSE(image.has_value());

        trace("malformed file: {}", image.error());
        CHECK(image.error().code == ErrorCode::INVALID_FORMAT);

        // The decoder says what was wrong with the bytes; the path is what the
        // reader needs in order to go and look at them.
        CHECK(image.error().message.find(file.path().string()) != std::string::npos);
    }
}

TEST_CASE("generated pixels are wrapped without being copied or checked twice",
          "[render][image]")
{
    SECTION("a buffer of exactly the right size is accepted")
    {
        std::vector<std::byte> pixels(4 * 3 * 1, std::byte{0x7f});

        const auto image = Image::from_pixels(std::move(pixels), 4, 3, PixelFormat::R8);
        REQUIRE(image.has_value());

        trace("wrapped a generated {}x{} {} image", image->width(), image->height(),
              to_string(image->format()));
        CHECK(image->size_in_bytes() == 12);
        CHECK(channel(*image, 0) == 0x7f);
    }

    SECTION("a buffer that merely holds enough is rejected")
    {
        std::vector<std::byte> pixels(4 * 3 * 4 + 1);

        const auto image = Image::from_pixels(std::move(pixels), 4, 3, PixelFormat::RGBA8);
        REQUIRE_FALSE(image.has_value());

        trace("oversized buffer: {}", image.error());
        CHECK(image.error().code == ErrorCode::INVALID_FORMAT);
    }

    SECTION("a dimension of zero is rejected before the size is even considered")
    {
        const auto image = Image::from_pixels({}, 0, 3, PixelFormat::RGBA8);
        REQUIRE_FALSE(image.has_value());

        trace("zero width: {}", image.error());
        CHECK(image.error().code == ErrorCode::INVALID_FORMAT);
    }
}
