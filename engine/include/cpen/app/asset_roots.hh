#ifndef CPEN_APP_ASSET_ROOTS_HH
#define CPEN_APP_ASSET_ROOTS_HH

#include "cpen/core/error.hh"

#include <expected>
#include <filesystem>
#include <span>
#include <string_view>

namespace cpen::app
{
    /// The directory the game's own files live in, relative to the executable.
    inline constexpr std::string_view GAME_ROOT_DIRECTORY = "game";

    /// The directory the engine's own files live in, relative to the executable.
    inline constexpr std::string_view ENGINE_ROOT_DIRECTORY = "engine";

    /// The two directories the virtual file system is mounted over.
    ///
    /// Two rather than one because they answer to different people: the game
    /// directory is written by whoever is making the novel, the engine directory
    /// ships with the engine and holds what a game can rely on always being there
    /// — the fallback typeface, and whatever else joins it. Mounted in this order,
    /// so a game that ships its own "fonts/ui.ttf" simply replaces the engine's.
    struct AssetRoots
    {
        std::filesystem::path game;
        std::filesystem::path engine;
    };

    /// Where the roots are when nobody says otherwise: beside the executable.
    ///
    ///     cpen_demo            the program
    ///     game/                assets/, script/, game.toml
    ///     engine/              assets/fonts/...
    ///
    /// Measured from the executable rather than from the working directory, which
    /// is whatever the shell or the launcher happened to be in — the difference
    /// between a game that runs when double-clicked and one that only runs from
    /// the directory it was built in. See platform::executable_path.
    ///
    /// The directories are not required to exist. This answers where they would
    /// be; whether they are there is reported when they are mounted, one warning
    /// per root, which is a far more useful failure than refusing to start.
    std::expected<AssetRoots, core::Error> default_asset_roots();

    /// Applies `--game <path>` and `--engine <path>` to `defaults`.
    ///
    /// Both forms are accepted — `--game path` and `--game=path` — because both
    /// are what people type. An argument that is not one of these is ignored
    /// rather than refused: a game is entitled to its own command line, and the
    /// engine has no business rejecting what it does not recognise.
    ///
    /// An override with nothing after it *is* refused. It is a typo with a silent
    /// failure mode otherwise: the game starts on the wrong data and reports every
    /// asset missing.
    ///
    /// A relative override is resolved against the working directory, not the
    /// executable: somebody typing a path into a terminal means the path they see.
    std::expected<AssetRoots, core::Error> apply_asset_root_arguments(
        std::span<const std::string_view> arguments, AssetRoots defaults);

    /// The two above, for a main() that has nothing else to say.
    std::expected<AssetRoots, core::Error> asset_roots_from_command_line(
        int argument_count, const char* const* arguments);
}

#endif //CPEN_APP_ASSET_ROOTS_HH
