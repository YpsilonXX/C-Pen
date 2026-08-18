#include "cpen/assets/asset_resolver.hh"

#include "cpen/assets/virtual_file_system.hh"
#include "cpen/assets/virtual_path.hh"
#include "cpen/core/log.hh"

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace cpen::assets
{
    namespace
    {
        // Ordered by preference. PNG before JPEG because a novel's artwork has
        // hard edges and flat areas, which is what JPEG is worst at and what an
        // author is most likely to notice too late.
        constexpr std::array<std::string_view, 3> PICTURE_EXTENSIONS = {"png", "jpg", "jpeg"};
        constexpr std::array<std::string_view, 2> FONT_EXTENSIONS = {"ttf", "otf"};
        constexpr std::array<std::string_view, 4> AUDIO_EXTENSIONS = {"ogg", "wav", "mp3", "flac"};
        constexpr std::array<std::string_view, 1> SCRIPT_EXTENSIONS = {"pen"};

        bool ends_with_extension(const std::string_view identifier,
                                 const std::string_view extension) noexcept
        {
            if (identifier.size() <= extension.size() + 1)
            {
                return false;
            }

            const std::size_t start = identifier.size() - extension.size();

            if (identifier[start - 1] != '.')
            {
                return false;
            }

            return std::ranges::equal(identifier.substr(start), extension,
                                      [](const char first, const char second)
            {
                const char folded = first >= 'A' && first <= 'Z'
                                        ? static_cast<char>(first - 'A' + 'a')
                                        : first;
                return folded == second;
            });
        }

        std::string alias_key(const AssetKind kind, const std::string_view identifier)
        {
            return std::format("{}:{}", to_string(kind), identifier);
        }
    }

    std::string_view to_string(const AssetKind kind) noexcept
    {
        switch (kind)
        {
            case AssetKind::BACKGROUND: return "background";
            case AssetKind::SPRITE:     return "sprite";
            case AssetKind::FONT:       return "font";
            case AssetKind::AUDIO:      return "audio";
            case AssetKind::SCRIPT:     return "script";
        }
        return "unknown";
    }

    std::string_view directory_of(const AssetKind kind) noexcept
    {
        switch (kind)
        {
            case AssetKind::BACKGROUND: return "assets/bg";
            case AssetKind::SPRITE:     return "assets/sprites";
            case AssetKind::FONT:       return "assets/fonts";
            case AssetKind::AUDIO:      return "assets/audio";

            // Scripts sit beside assets rather than inside them: they are the game
            // rather than something the game shows, and they are the one kind an
            // author edits by hand every day.
            case AssetKind::SCRIPT:     return "script";
        }
        return "assets";
    }

    std::span<const std::string_view> extensions_of(const AssetKind kind) noexcept
    {
        switch (kind)
        {
            case AssetKind::BACKGROUND:
            case AssetKind::SPRITE:     return PICTURE_EXTENSIONS;
            case AssetKind::FONT:       return FONT_EXTENSIONS;
            case AssetKind::AUDIO:      return AUDIO_EXTENSIONS;
            case AssetKind::SCRIPT:     return SCRIPT_EXTENSIONS;
        }
        return {};
    }

    std::string compose_virtual_path(const AssetKind kind, const std::string_view identifier,
                                     std::string_view extension)
    {
        std::string path{directory_of(kind)};
        path += '/';
        path += identifier;

        // A caller that writes ".png" means the same thing as one that writes
        // "png", and getting "..png" for the difference would be a poor joke.
        if (extension.starts_with('.'))
        {
            extension.remove_prefix(1);
        }

        if (!extension.empty())
        {
            path += '.';
            path += extension;
        }

        return path;
    }

    std::expected<void, core::Error> validate_asset_id(const AssetKind kind,
                                                       const std::string_view identifier)
    {
        for (const std::string_view extension : extensions_of(kind))
        {
            if (ends_with_extension(identifier, extension))
            {
                return std::unexpected(core::make_error(
                    core::ErrorCode::INVALID_FORMAT,
                    "asset id '{}' ends in '.{}'; identifiers carry no extension, because "
                    "finding the file type is the engine's job — write '{}'",
                    identifier, extension,
                    identifier.substr(0, identifier.size() - extension.size() - 1)));
            }
        }

        // Everything else an identifier must not be, it must not be for the same
        // reasons a path must not be it, so the rules are not written twice.
        if (const std::expected<void, core::Error> valid = validate_virtual_path(identifier);
            !valid.has_value())
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT, "asset id '{}' cannot name a file: {}",
                identifier, valid.error().message));
        }

        return {};
    }

    std::expected<void, core::Error> AssetResolver::add_alias(const AssetKind kind,
                                                              const std::string_view identifier,
                                                              const std::string_view virtual_path)
    {
        if (const std::expected<void, core::Error> valid = validate_asset_id(kind, identifier);
            !valid.has_value())
        {
            return valid;
        }

        if (const std::expected<void, core::Error> valid = validate_virtual_path(virtual_path);
            !valid.has_value())
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT,
                "alias for {} '{}': {}", to_string(kind), identifier, valid.error().message));
        }

        this->aliases.insert_or_assign(alias_key(kind, identifier), std::string{virtual_path});
        return {};
    }

    std::expected<std::string, core::Error> AssetResolver::resolve(
        const AssetKind kind, const std::string_view identifier) const
    {
        if (const std::expected<void, core::Error> valid = validate_asset_id(kind, identifier);
            !valid.has_value())
        {
            return std::unexpected(valid.error());
        }

        if (const auto alias = this->aliases.find(alias_key(kind, identifier));
            alias != this->aliases.end())
        {
            if (!this->file_system->exists(alias->second))
            {
                return std::unexpected(core::make_error(
                    core::ErrorCode::FILE_NOT_FOUND,
                    "{} '{}' is aliased to '{}', and there is no such file",
                    to_string(kind), identifier, alias->second));
            }

            return alias->second;
        }

        // Probing stops at the first hit unless the path audit is on, in which
        // case every extension is tried so that two files answering to one
        // identifier can be reported. That is the same bargain the case audit
        // makes: the author's machine pays for the diagnostics, the player's does
        // not.
        const bool probe_all = this->file_system->path_audit();

        std::vector<std::string> found;
        std::string attempted;

        for (const std::string_view extension : extensions_of(kind))
        {
            std::string candidate = compose_virtual_path(kind, identifier, extension);

            if (!attempted.empty())
            {
                attempted += ", ";
            }
            attempted += candidate;

            if (this->file_system->exists(candidate))
            {
                found.push_back(std::move(candidate));

                if (!probe_all)
                {
                    break;
                }
            }
        }

        if (found.empty())
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::FILE_NOT_FOUND, "no {} '{}': tried {}",
                to_string(kind), identifier, attempted));
        }

        if (found.size() > 1 && this->reported_ambiguities.emplace(identifier).second)
        {
            std::string listed;

            for (const std::string& candidate : found)
            {
                if (!listed.empty())
                {
                    listed += ", ";
                }
                listed += candidate;
            }

            // Deterministic, so not a failure — but an author who converted a
            // picture and left the original behind is editing a file the game
            // never opens, and nothing else would ever tell them.
            log::warn(log::Category::ASSETS,
                      "{} '{}' matches more than one file ({}); using '{}'. Delete the "
                      "ones that are no longer wanted, or the game ships all of them",
                      to_string(kind), identifier, listed, found.front());
        }

        return found.front();
    }
}
