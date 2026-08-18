#ifndef CPEN_ASSETS_ASSET_MANAGER_HH
#define CPEN_ASSETS_ASSET_MANAGER_HH

#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/asset_store.hh"
#include "cpen/core/error.hh"
#include "cpen/render/font.hh"
#include "cpen/render/image.hh"
#include "cpen/render/texture.hh"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cpen::assets
{
    class VirtualFileSystem;

    using ImageReference = AssetReference<render::Image>;
    using TextureReference = AssetReference<render::Texture>;
    using FontReference = AssetReference<render::Font>;

    /// An asset that was asked for and could not be had.
    struct MissingAsset
    {
        AssetKind kind = AssetKind::SPRITE;
        std::string identifier;
        std::string reason;
    };

    /// Loads assets by identifier, keeps them, and hands out references to them.
    ///
    /// The three layers below each answer one question and this one asks them in
    /// order: the resolver turns "alice/happy" into a virtual path, the file
    /// system turns that into bytes, and the render layer turns bytes into
    /// something the driver holds. What this class adds is that the second
    /// request for the same asset costs nothing, and that a caller can hold an
    /// asset without owning it.
    ///
    /// **Constructing one makes no GL call**, and neither does any part of it
    /// until a Texture or a Font is actually asked for. That is load-bearing, not
    /// incidental: the manager belongs in GameContext beside the renderer, and
    /// the state and stack tests that use a GameContext must keep running with no
    /// window in existence. Images, identifiers, caching, reference counting and
    /// the missing-asset ledger are all reachable without a driver.
    ///
    /// Images are cached like everything else, but asking for a Texture does not
    /// leave one behind: the pixels are decoded, uploaded and dropped. A
    /// background kept on both sides of the bus costs eight megabytes for the
    /// half nothing reads. A caller who wants the pixels — to test a click
    /// against a character's outline, say — asks for the Image itself and gets
    /// one that is kept.
    ///
    /// Failures are reported, never substituted. A missing sprite comes back as
    /// an error, and what to draw instead is decided by the layer that knows what
    /// is on screen; placeholder_texture() is what it draws. The alternative — a
    /// load that always succeeds — makes "the asset is there" and "the asset is a
    /// pink square" indistinguishable to every caller, including the one writing
    /// the log line.
    class AssetManager
    {
    public:
        AssetManager(const VirtualFileSystem& files,
                     const AssetResolver& asset_resolver) noexcept
            : file_system(&files), resolver(&asset_resolver) {}

        /// Decoded pixels, kept. `kind` is BACKGROUND or SPRITE — the same
        /// picture under two kinds is two assets, because it is two files.
        std::expected<ImageReference, core::Error> image(AssetKind kind,
                                                         std::string_view identifier);

        /// A picture on the GPU. Requires a current GL context.
        std::expected<TextureReference, core::Error> texture(AssetKind kind,
                                                             std::string_view identifier);

        /// A typeface at one size. Two sizes are two assets with two atlases,
        /// because that is what a Font is; the size is part of the cache key
        /// rather than a parameter to drawing.
        std::expected<FontReference, core::Error> font(std::string_view identifier,
                                                       std::uint32_t pixel_size);

        /// The picture to draw where an asset is missing, or nullptr if even this
        /// could not be created — which means there is no GL context, and the
        /// caller has larger problems than a missing sprite.
        ///
        /// Generated on first use and kept for the run. Not in the store: it has
        /// no identifier, cannot be collected, and must stay valid for exactly as
        /// long as the manager does.
        const render::Texture* placeholder_texture();

        /// Unloads everything nobody holds, across all three caches, and answers
        /// how many assets went. Meant for a chapter boundary rather than a frame
        /// boundary.
        std::size_t collect_unused();

        /// Every asset that was asked for and could not be had, once each.
        std::span<const MissingAsset> missing() const noexcept { return this->missing_assets; }

        /// The closing report, empty when nothing was missing. Printed at exit for
        /// the same reason the case-mismatch summary is: a failure at minute three
        /// of a playtest is a thousand log lines above the end.
        std::string format_missing_summary() const;

        /// Counts, for tests and for a diagnostic overlay later.
        std::size_t loaded_image_count() const noexcept { return this->images.loaded_count(); }
        std::size_t loaded_texture_count() const noexcept { return this->textures.loaded_count(); }
        std::size_t loaded_font_count() const noexcept { return this->fonts.loaded_count(); }

    private:
        /// Records and logs a failure, once per identifier, and hands the error
        /// back unchanged for returning.
        core::Error note_missing(AssetKind kind, std::string_view identifier,
                                 core::Error error);

        const VirtualFileSystem* file_system;
        const AssetResolver* resolver;

        AssetStore<render::Image> images;
        AssetStore<render::Texture> textures;
        AssetStore<render::Font> fonts;

        std::optional<render::Texture> placeholder;
        bool placeholder_failed = false;

        std::vector<MissingAsset> missing_assets;
    };
}

#endif //CPEN_ASSETS_ASSET_MANAGER_HH
