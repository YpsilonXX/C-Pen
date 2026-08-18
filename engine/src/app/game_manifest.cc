#include "cpen/app/game_manifest.hh"

#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/log.hh"

#include <toml.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace cpen::app
{
    namespace
    {
        /// Complains about a key nobody reads.
        ///
        /// A manifest is edited by hand and never compiled, so a misspelt key is
        /// otherwise perfectly silent: the game starts, the setting does nothing,
        /// and the author looks for the bug in the engine. Every table checks its
        /// own keys against what it understands.
        void report_unknown_keys(const toml::table& table, const std::string_view where,
                                 const std::span<const std::string_view> known,
                                 const std::string_view source_name)
        {
            for (const auto& [key, value] : table)
            {
                static_cast<void>(value);

                const std::string_view name{key.str()};

                if (std::ranges::find(known, name) == known.end())
                {
                    log::warn(log::Category::APP, "{}: nothing reads '{}{}'", source_name,
                              where, name);
                }
            }
        }

        /// Reads a string, leaving the destination alone if the key is absent and
        /// reporting it if the key is there and is not a string.
        void read_string(const toml::table& table, const std::string_view key,
                         std::string& destination, const std::string_view where,
                         const std::string_view source_name)
        {
            const toml::node* const node = table.get(key);

            if (node == nullptr)
            {
                return;
            }

            if (const std::optional<std::string> value = node->value<std::string>())
            {
                destination = *value;
                return;
            }

            log::warn(log::Category::APP, "{}: '{}{}' is not text; keeping '{}'", source_name,
                      where, key, destination);
        }

        /// The same for a size in pixels. Zero is refused along with the wrong
        /// type: every quantity derived from a resolution divides by it.
        void read_dimension(const toml::table& table, const std::string_view key,
                            std::uint32_t& destination, const std::string_view where,
                            const std::string_view source_name)
        {
            const toml::node* const node = table.get(key);

            if (node == nullptr)
            {
                return;
            }

            const std::optional<std::int64_t> value = node->value<std::int64_t>();

            if (value.has_value() && *value > 0 &&
                *value <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                destination = static_cast<std::uint32_t>(*value);
                return;
            }

            log::warn(log::Category::APP, "{}: '{}{}' is not a size in pixels; keeping {}",
                      source_name, where, key, destination);
        }

        void read_scale_mode(const toml::table& table, const std::string_view key,
                             render::ScaleMode& destination, const std::string_view where,
                             const std::string_view source_name)
        {
            const toml::node* const node = table.get(key);

            if (node == nullptr)
            {
                return;
            }

            const std::optional<std::string> value = node->value<std::string>();

            if (value == to_string(render::ScaleMode::LETTERBOX))
            {
                destination = render::ScaleMode::LETTERBOX;
                return;
            }

            if (value == to_string(render::ScaleMode::STRETCH))
            {
                destination = render::ScaleMode::STRETCH;
                return;
            }

            log::warn(log::Category::APP,
                      "{}: '{}{}' is neither '{}' nor '{}'; keeping '{}'", source_name, where,
                      key, to_string(render::ScaleMode::LETTERBOX),
                      to_string(render::ScaleMode::STRETCH), to_string(destination));
        }

        /// A sub-table, or nullptr when the key is absent — and a complaint when
        /// it is present as something other than a table, since `[window]` written
        /// as `window = "big"` reads as a section that is quietly ignored.
        const toml::table* section(const toml::table& root, const std::string_view key,
                                   const std::string_view source_name)
        {
            const toml::node* const node = root.get(key);

            if (node == nullptr)
            {
                return nullptr;
            }

            if (const toml::table* const nested = node->as_table())
            {
                return nested;
            }

            log::warn(log::Category::APP, "{}: '{}' is not a section; it is ignored",
                      source_name, key);

            return nullptr;
        }
    }

    std::expected<GameManifest, core::Error> GameManifest::parse(
        const std::string_view text, const std::string_view source_name)
    {
        const toml::parse_result parsed = toml::parse(text, source_name);

        if (!parsed)
        {
            const toml::parse_error& error = parsed.error();

            // The one failure that is not survivable: with the syntax broken,
            // nothing in the file can be trusted to mean what it appears to, and
            // starting on half a manifest is worse than not starting.
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT, "{}:{}:{}: {}", source_name,
                error.source().begin.line, error.source().begin.column,
                std::string_view{error.description()}));
        }

        const toml::table& root = parsed.table();

        GameManifest manifest;

        constexpr std::array<std::string_view, 5> ROOT_KEYS = {
            "name", "version", "window", "screen", "story",
        };
        report_unknown_keys(root, "", ROOT_KEYS, source_name);

        read_string(root, "name", manifest.name, "", source_name);
        read_string(root, "version", manifest.version, "", source_name);

        if (const toml::table* const window = section(root, "window", source_name))
        {
            constexpr std::array<std::string_view, 3> WINDOW_KEYS = {"title", "width", "height"};
            report_unknown_keys(*window, "window.", WINDOW_KEYS, source_name);

            read_string(*window, "title", manifest.window.title, "window.", source_name);
            read_dimension(*window, "width", manifest.window.width, "window.", source_name);
            read_dimension(*window, "height", manifest.window.height, "window.", source_name);
        }

        if (const toml::table* const screen = section(root, "screen", source_name))
        {
            constexpr std::array<std::string_view, 3> SCREEN_KEYS = {"width", "height", "scale"};
            report_unknown_keys(*screen, "screen.", SCREEN_KEYS, source_name);

            read_dimension(*screen, "width", manifest.screen.width, "screen.", source_name);
            read_dimension(*screen, "height", manifest.screen.height, "screen.", source_name);
            read_scale_mode(*screen, "scale", manifest.screen.scale_mode, "screen.",
                            source_name);
        }

        if (const toml::table* const story = section(root, "story", source_name))
        {
            constexpr std::array<std::string_view, 2> STORY_KEYS = {"script", "label"};
            report_unknown_keys(*story, "story.", STORY_KEYS, source_name);

            read_string(*story, "script", manifest.story.script, "story.", source_name);
            read_string(*story, "label", manifest.story.label, "story.", source_name);
        }

        // Filled in last rather than defaulted in the struct, because it depends
        // on another field: a game that says nothing about its window still has
        // its own name on it.
        if (manifest.window.title.empty())
        {
            manifest.window.title = manifest.name;
        }

        return manifest;
    }

    std::expected<GameManifest, core::Error> GameManifest::read(
        const assets::VirtualFileSystem& files, const std::string_view virtual_path)
    {
        return files.read_text(virtual_path)
            .and_then([virtual_path](const std::string& text)
            {
                return parse(text, virtual_path);
            });
    }

    std::expected<GameManifest, core::Error> GameManifest::read(
        const AssetRoots& roots, const std::string_view virtual_path)
    {
        assets::VirtualFileSystem files;

        // The game's root first and the engine's second, the order the Application
        // mounts them in, so that a game's manifest wins over anything the engine
        // ships under the same name.
        files.mount(roots.game);
        files.mount(roots.engine);

        return read(files, virtual_path);
    }

    Application::Config configuration_from_manifest(const GameManifest& manifest,
                                                    AssetRoots roots)
    {
        Application::Config configuration;

        configuration.window.title = manifest.window.title;
        configuration.window.width = manifest.window.width;
        configuration.window.height = manifest.window.height;

        configuration.virtual_width = manifest.screen.width;
        configuration.virtual_height = manifest.screen.height;
        configuration.scale_mode = manifest.screen.scale_mode;

        configuration.roots = std::move(roots);

        return configuration;
    }
}
