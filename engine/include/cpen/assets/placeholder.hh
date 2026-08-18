#ifndef CPEN_ASSETS_PLACEHOLDER_HH
#define CPEN_ASSETS_PLACEHOLDER_HH

#include "cpen/render/image.hh"

#include <cstdint>

namespace cpen::assets
{
    /// The picture shown where an asset should have been.
    ///
    /// Generated rather than loaded, and that is the whole requirement: a
    /// placeholder read from a file is a placeholder that can itself be missing,
    /// which is precisely the situation it exists to survive. It needs no GL
    /// context either, so what it looks like is decided in a test rather than by
    /// looking at a screen.
    ///
    /// Magenta and black in a checkerboard, because it has to be a colour nobody
    /// chose on purpose. A grey box reads as art direction; this reads as a
    /// mistake, which is what it is.
    ///
    /// Opaque throughout: a transparent placeholder for a missing character
    /// sprite would leave an empty screen, and an empty screen is what a missing
    /// asset looks like when nothing is drawn at all.
    render::Image make_placeholder_image(std::uint32_t width = 64, std::uint32_t height = 64,
                                         std::uint32_t cell = 8);
}

#endif //CPEN_ASSETS_PLACEHOLDER_HH
