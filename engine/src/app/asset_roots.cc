#include "cpen/app/asset_roots.hh"

#include "cpen/core/file_system.hh"
#include "cpen/platform/executable_path.hh"

#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace cpen::app
{
    namespace
    {
        constexpr std::string_view GAME_OPTION = "--game";
        constexpr std::string_view ENGINE_OPTION = "--engine";

        /// Resolves an override against the working directory and tidies it, so
        /// that later messages name something the reader can paste back.
        std::filesystem::path resolve_override(const std::string_view value)
        {
            std::error_code error;
            std::filesystem::path path = core::path_from_utf8(value);
            std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);

            return error ? std::move(path) : std::move(resolved);
        }
    }

    std::expected<AssetRoots, core::Error> default_asset_roots()
    {
        return platform::executable_directory().transform(
            [](const std::filesystem::path& directory)
            {
                return AssetRoots{
                    .game = directory / GAME_ROOT_DIRECTORY,
                    .engine = directory / ENGINE_ROOT_DIRECTORY,
                };
            });
    }

    std::expected<AssetRoots, core::Error> apply_asset_root_arguments(
        const std::span<const std::string_view> arguments, AssetRoots defaults)
    {
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            const std::string_view argument = arguments[index];

            std::filesystem::path* target = nullptr;

            if (argument.starts_with(GAME_OPTION))
            {
                target = &defaults.game;
            }
            else if (argument.starts_with(ENGINE_OPTION))
            {
                target = &defaults.engine;
            }

            if (target == nullptr)
            {
                continue;
            }

            const std::string_view option =
                target == &defaults.game ? GAME_OPTION : ENGINE_OPTION;

            // Guards against --gamepad being read as --game: an option is the
            // whole argument, or the whole argument up to an '='.
            if (argument.size() > option.size() && argument[option.size()] != '=')
            {
                continue;
            }

            if (argument.size() > option.size())
            {
                const std::string_view value = argument.substr(option.size() + 1);

                if (value.empty())
                {
                    return std::unexpected(core::make_error(
                        core::ErrorCode::INVALID_FORMAT, "{}= needs a directory after it",
                        option));
                }

                *target = resolve_override(value);
                continue;
            }

            if (index + 1 >= arguments.size())
            {
                return std::unexpected(core::make_error(
                    core::ErrorCode::INVALID_FORMAT, "{} needs a directory after it", option));
            }

            *target = resolve_override(arguments[++index]);
        }

        return defaults;
    }

    std::expected<AssetRoots, core::Error> asset_roots_from_command_line(
        const int argument_count, const char* const* const arguments)
    {
        std::vector<std::string_view> listed;

        // argv[0] is the program's own name, not an option.
        for (int index = 1; index < argument_count; ++index)
        {
            if (arguments[index] != nullptr)
            {
                listed.emplace_back(arguments[index]);
            }
        }

        return default_asset_roots().and_then([&listed](AssetRoots defaults)
        {
            return apply_asset_root_arguments(listed, std::move(defaults));
        });
    }
}
