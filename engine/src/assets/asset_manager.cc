#include "cpen/assets/asset_manager.hh"

#include "cpen/assets/placeholder.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/log.hh"
#include "cpen/script/diagnostic.hh"

#include <algorithm>
#include <format>
#include <utility>

namespace cpen::assets
{
    std::expected<ImageReference, core::Error> AssetManager::image(
        const AssetKind kind, const std::string_view identifier)
    {
        std::expected<std::string, core::Error> path = this->resolver->resolve(kind, identifier);

        if (!path.has_value())
        {
            return std::unexpected(
                this->note_missing(kind, identifier, std::move(path.error())));
        }

        // The virtual path is the cache key rather than the identifier: two
        // identifiers aliased to one file are one asset, and one identifier that
        // the manifest re-points is a different one.
        std::expected<AssetHandle<render::Image>, core::Error> handle =
            this->images.acquire(*path, [this, &path]
            {
                return this->file_system->read(*path).and_then(
                    [](const std::vector<std::byte>& encoded)
                    {
                        return render::Image::from_memory(encoded);
                    });
            });

        if (!handle.has_value())
        {
            return std::unexpected(
                this->note_missing(kind, identifier, std::move(handle.error())));
        }

        return ImageReference{this->images, *handle};
    }

    std::expected<TextureReference, core::Error> AssetManager::texture(
        const AssetKind kind, const std::string_view identifier)
    {
        std::expected<std::string, core::Error> path = this->resolver->resolve(kind, identifier);

        if (!path.has_value())
        {
            return std::unexpected(
                this->note_missing(kind, identifier, std::move(path.error())));
        }

        std::expected<AssetHandle<render::Texture>, core::Error> handle =
            this->textures.acquire(*path, [this, &path]
            {
                // The decoded pixels are a step, not a product: they are uploaded
                // and dropped here. Keeping them would double what a background
                // costs for the half that nothing reads afterwards.
                return this->file_system->read(*path)
                    .and_then([](const std::vector<std::byte>& encoded)
                    {
                        return render::Image::from_memory(encoded);
                    })
                    .and_then([](render::Image decoded)
                    {
                        // The one place this happens, and the reason the load
                        // path exists at all rather than callers making textures
                        // themselves: everything the sprite batch draws has to be
                        // in premultiplied space, because that is the space it
                        // blends in.
                        decoded.premultiply_alpha();
                        return render::Texture::from_image(decoded);
                    });
            });

        if (!handle.has_value())
        {
            return std::unexpected(
                this->note_missing(kind, identifier, std::move(handle.error())));
        }

        return TextureReference{this->textures, *handle};
    }

    std::expected<FontReference, core::Error> AssetManager::font(
        const std::string_view identifier, const std::uint32_t pixel_size)
    {
        std::expected<std::string, core::Error> path =
            this->resolver->resolve(AssetKind::FONT, identifier);

        if (!path.has_value())
        {
            return std::unexpected(
                this->note_missing(AssetKind::FONT, identifier, std::move(path.error())));
        }

        // The size is in the key because it is in the asset: a Font is one
        // typeface at one size with one atlas, and asking for another size is
        // asking for another asset, not for a different way of drawing this one.
        const std::string key = std::format("{}@{}", *path, pixel_size);

        std::expected<AssetHandle<render::Font>, core::Error> handle =
            this->fonts.acquire(key, [this, &path, pixel_size]
            {
                return this->file_system->read(*path).and_then(
                    [&path, pixel_size](const std::vector<std::byte>& data)
                    {
                        return render::Font::from_memory(data, pixel_size, render::FontConfig{},
                                                         *path);
                    });
            });

        if (!handle.has_value())
        {
            return std::unexpected(this->note_missing(
                AssetKind::FONT, std::format("{}@{}", identifier, pixel_size),
                std::move(handle.error())));
        }

        return FontReference{this->fonts, *handle};
    }

    std::expected<ScriptReference, core::Error> AssetManager::script(
        const std::string_view identifier)
    {
        std::expected<std::string, core::Error> path =
            this->resolver->resolve(AssetKind::SCRIPT, identifier);

        if (!path.has_value())
        {
            return std::unexpected(
                this->note_missing(AssetKind::SCRIPT, identifier, std::move(path.error())));
        }

        std::expected<AssetHandle<script::Script>, core::Error> handle =
            this->scripts.acquire(*path, [this, &path]
            {
                return this->file_system->read_text(*path).and_then(
                    [&path](std::string source) -> std::expected<script::Script, core::Error>
                    {
                        // The text is handed over as a copy and kept here as
                        // well, because a diagnostic indexes into the exact string
                        // it was produced from: on failure there is no Script left
                        // holding the source, and re-reading the file to render
                        // the message would move every caret if it had changed.
                        std::expected<script::Script, std::vector<script::Diagnostic>> compiled =
                            script::make_script(*path, source);

                        if (compiled.has_value())
                        {
                            return std::move(*compiled);
                        }

                        return std::unexpected(core::make_error(
                            core::ErrorCode::COMPILATION_FAILED, "{}",
                            script::render_diagnostics(compiled.error(), source, *path)));
                    });
            });

        if (!handle.has_value())
        {
            return std::unexpected(
                this->note_missing(AssetKind::SCRIPT, identifier, std::move(handle.error())));
        }

        return ScriptReference{this->scripts, *handle};
    }

    const render::Texture* AssetManager::placeholder_texture()
    {
        if (this->placeholder.has_value())
        {
            return &*this->placeholder;
        }

        if (this->placeholder_failed)
        {
            return nullptr;
        }

        std::expected<render::Texture, core::Error> created = render::Texture::from_image(
            make_placeholder_image(),
            render::TextureConfig{
                // Nearest, so the checkerboard stays a checkerboard however far it
                // is stretched. A blurred placeholder reads as a broken picture
                // rather than as a missing one.
                .minify_filter = render::TextureFilter::NEAREST,
                .magnify_filter = render::TextureFilter::NEAREST,
            });

        if (!created.has_value())
        {
            this->placeholder_failed = true;

            log::error(log::Category::ASSETS,
                       "the placeholder texture could not be created ({}); there is nothing "
                       "to draw in place of a missing asset", created.error());

            return nullptr;
        }

        this->placeholder.emplace(std::move(*created));
        return &*this->placeholder;
    }

    std::size_t AssetManager::collect_unused()
    {
        const std::size_t collected = this->images.collect_unused() +
                                      this->textures.collect_unused() +
                                      this->fonts.collect_unused() +
                                      this->scripts.collect_unused();

        if (collected > 0)
        {
            log::info(log::Category::ASSETS, "unloaded {} asset(s) nothing was holding",
                      collected);
        }

        return collected;
    }

    core::Error AssetManager::note_missing(const AssetKind kind,
                                           const std::string_view identifier,
                                           core::Error error)
    {
        const bool known = std::ranges::any_of(this->missing_assets,
                                               [kind, identifier](const MissingAsset& missing)
        {
            return missing.kind == kind && missing.identifier == identifier;
        });

        if (!known)
        {
            log::error(log::Category::ASSETS, "{} '{}' could not be loaded: {}",
                       to_string(kind), identifier, error);

            this->missing_assets.push_back(MissingAsset{
                .kind = kind,
                .identifier = std::string{identifier},
                .reason = error.message,
            });
        }

        return error;
    }

    std::string AssetManager::format_missing_summary() const
    {
        if (this->missing_assets.empty())
        {
            return {};
        }

        std::string summary = std::format(
            "\n"
            "!!! ==================================================================== !!!\n"
            "!!!  {} ASSET(S) WERE ASKED FOR AND COULD NOT BE LOADED\n"
            "!!! ==================================================================== !!!\n",
            this->missing_assets.size());

        for (const MissingAsset& missing : this->missing_assets)
        {
            summary += std::format("  {} '{}'\n      {}\n", to_string(missing.kind),
                                   missing.identifier, missing.reason);
        }

        summary +=
            "!!! ==================================================================== !!!\n";

        return summary;
    }
}
