#include "cpen/assets/placeholder.hh"

#include "cpen/core/log.hh"
#include "cpen/render/pixel_format.hh"

#include <array>
#include <cstddef>
#include <vector>

namespace cpen::assets
{
    namespace
    {
        constexpr std::array<std::uint8_t, 4> MAGENTA = {255, 0, 220, 255};
        constexpr std::array<std::uint8_t, 4> BLACK = {24, 24, 24, 255};
    }

    render::Image make_placeholder_image(const std::uint32_t width, const std::uint32_t height,
                                         const std::uint32_t cell)
    {
        const std::uint32_t safe_width = width == 0 ? 1 : width;
        const std::uint32_t safe_height = height == 0 ? 1 : height;
        const std::uint32_t safe_cell = cell == 0 ? 1 : cell;

        std::vector<std::byte> pixels(render::image_size_in_bytes(
            safe_width, safe_height, render::PixelFormat::RGBA8));

        std::size_t offset = 0;

        for (std::uint32_t row = 0; row < safe_height; ++row)
        {
            for (std::uint32_t column = 0; column < safe_width; ++column)
            {
                const bool light = ((row / safe_cell) + (column / safe_cell)) % 2 == 0;

                for (const std::uint8_t channel : light ? MAGENTA : BLACK)
                {
                    pixels[offset++] = static_cast<std::byte>(channel);
                }
            }
        }

        std::expected<render::Image, core::Error> image = render::Image::from_pixels(
            std::move(pixels), safe_width, safe_height, render::PixelFormat::RGBA8);

        if (!image.has_value())
        {
            // The buffer was sized by the same function Image checks it against,
            // so this cannot happen without one of the two having been changed
            // and not the other.
            log::fatal(log::Category::ASSETS, "the placeholder image is malformed: {}",
                       image.error());
        }

        return std::move(*image);
    }
}
