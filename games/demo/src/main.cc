#include <cpen/app/application.hh>
#include <cpen/app/asset_roots.hh>
#include <cpen/app/game_manifest.hh>
#include <cpen/core/log.hh>
#include <cpen/runtime/dialogue_state.hh>

#include <memory>
#include <utility>

using namespace cpen;

/// Entry point for the C-Pen demo game executable.
///
/// Almost nothing: the game is a manifest, a script and a directory of pictures,
/// and this file exists only to say which directory. That is the claim the whole
/// phase was built to make — a visual novel needs no C++ of its own, and the day
/// one does need some, it will be a behaviour registered beside this, not a loop
/// written instead of the engine's.
///
/// The command line is read for one reason: to say where the game's files are.
/// Without it they are the ones beside this executable, which is what a player
/// gets; with `--game <path>` the same binary runs a different game directory,
/// which is what an author editing one wants.
int main(const int argument_count, const char* const* const arguments)
{
    log::initialize_console();
    log::info(log::Category::APP, "C-Pen demo starting");

    auto roots = app::asset_roots_from_command_line(argument_count, arguments);

    if (!roots)
    {
        // Fatal, unlike almost everything else in the engine. Every other failure
        // leaves a window up with a diagnostic in it; this one means the player
        // asked for a directory and did not say which, and starting on the wrong
        // one would report every asset in the game as missing.
        log::error(log::Category::APP, "{}", roots.error());
        return 1;
    }

    // Read before the Application is built, not after: the manifest is what says
    // how large a window to open, so it has to be in hand before there is one.
    auto manifest = app::GameManifest::read(*roots);

    if (!manifest)
    {
        log::error(log::Category::APP, "{}", manifest.error());
        return 1;
    }

    if (manifest->story.script.empty())
    {
        log::error(log::Category::APP,
                   "the manifest names no script under [story]; there is nothing to play");
        return 1;
    }

    app::Application application{app::configuration_from_manifest(*manifest, std::move(*roots))};

    // The whole game, in one push. Everything after this belongs to the engine:
    // the dialogue state loads the script, runs it, draws it and takes it down
    // when the story ends — and an empty stack is what ends the loop.
    application.states().push(std::make_unique<runtime::DialogueState>(
        runtime::DialogueState::Config{
            .script = manifest->story.script,
            .label = manifest->story.label,
        }));

    application.run();

    log::info(log::Category::APP, "C-Pen demo shutting down");
    return 0;
}
