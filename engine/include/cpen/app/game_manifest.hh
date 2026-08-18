#ifndef CPEN_APP_GAME_MANIFEST_HH
#define CPEN_APP_GAME_MANIFEST_HH

#include "cpen/app/application.hh"
#include "cpen/app/asset_roots.hh"
#include "cpen/core/error.hh"
#include "cpen/render/viewport.hh"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace cpen::assets
{
    class VirtualFileSystem;
}

namespace cpen::app
{
    /// What a game says about itself: its name, the window it wants, the screen
    /// it was drawn for, and where its story begins.
    ///
    /// A file rather than code because none of it is the engine's decision, and
    /// because changing any of it must not mean rebuilding: an author retitling
    /// a game or moving its opening chapter edits one line and runs the same
    /// binary. It is read through the virtual file system like everything else,
    /// so a game's manifest replaces the engine's the way a game's typeface
    /// replaces the engine's.
    ///
    /// Every field has a default and a malformed one is reported and replaced
    /// rather than refused. A manifest that would not load is a game that will
    /// not start, and a mistyped window height is not worth that; only a file
    /// that is not TOML at all fails outright, because then nothing in it can be
    /// trusted to mean what it appears to.
    struct GameManifest
    {
        /// Where a game keeps it, at the root of the game directory beside
        /// `assets/` and `script/`.
        static constexpr std::string_view DEFAULT_PATH = "game.toml";

        struct WindowSettings
        {
            /// What the title bar says. Empty means the game's name.
            std::string title{};

            std::uint32_t width = 1280;
            std::uint32_t height = 720;
        };

        /// The resolution the game was drawn for, which is the space every
        /// position in every script means something in. Unrelated to the window
        /// above: that one is where the game opens, this one is a property of the
        /// artwork and never changes.
        struct ScreenSettings
        {
            std::uint32_t width = render::Viewport::DEFAULT_VIRTUAL_WIDTH;
            std::uint32_t height = render::Viewport::DEFAULT_VIRTUAL_HEIGHT;
            render::ScaleMode scale_mode = render::ScaleMode::LETTERBOX;
        };

        struct StorySettings
        {
            /// The script to play, as an asset identifier — "intro", not a path.
            std::string script{};

            /// The label to begin at. Empty starts at the top of the file.
            std::string label{};
        };

        std::string name{};
        std::string version{};

        WindowSettings window{};
        ScreenSettings screen{};
        StorySettings story{};

        /// Reads a manifest out of text. `source_name` appears in diagnostics and
        /// is the file the text came from.
        static std::expected<GameManifest, core::Error> parse(std::string_view text,
                                                              std::string_view source_name);

        /// Reads the manifest out of a mounted file system.
        static std::expected<GameManifest, core::Error> read(
            const assets::VirtualFileSystem& files,
            std::string_view virtual_path = DEFAULT_PATH);

        /// Reads the manifest given only the roots, mounting them as the
        /// Application will.
        ///
        /// Exists because of an ordering problem worth stating: the manifest says
        /// how large a window to open, so it has to be read before the Application
        /// that would have mounted the roots is built.
        static std::expected<GameManifest, core::Error> read(
            const AssetRoots& roots, std::string_view virtual_path = DEFAULT_PATH);
    };

    /// The application settings a manifest describes, with the roots it was found
    /// through.
    ///
    /// The one place the two vocabularies meet, so that neither has to know the
    /// other: a manifest is what an author writes, a Config is what the engine
    /// starts from.
    Application::Config configuration_from_manifest(const GameManifest& manifest,
                                                    AssetRoots roots);
}

#endif //CPEN_APP_GAME_MANIFEST_HH
