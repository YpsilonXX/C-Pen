#ifndef CPEN_SCRIPT_COMMAND_SINK_HH
#define CPEN_SCRIPT_COMMAND_SINK_HH

#include <optional>
#include <string>
#include <vector>

namespace cpen::script
{
    /// A line to be shown, with its speaker resolved. An absent speaker is
    /// narration.
    struct SayCommand
    {
        std::optional<std::string> speaker{};
        std::string text{};
    };

    /// The choices to offer, in the order the script writes them. Which one the
    /// player took comes back through the machine's resume, not through here:
    /// this channel only ever runs one way.
    struct MenuCommand
    {
        std::vector<std::string> prompts{};
    };

    /// Where a sprite goes, in the normalised coordinates the author writes:
    /// (0, 0) is the top left of the reference screen and (1, 1) the bottom
    /// right, so a position means the same thing at every window size.
    struct ScreenPosition
    {
        double x = 0.0;
        double y = 0.0;
    };

    /// A change of background. An empty transition means an immediate cut.
    struct SceneCommand
    {
        std::string background{};
        std::string transition{};
    };

    /// A sprite put on screen, addressed by the asset name the script's words
    /// resolve to ("alice/happy").
    ///
    /// The two ways of placing it are deliberately separate rather than one
    /// resolved position: an anchor is a name the presentation layer is free to
    /// interpret — and to reinterpret when the layout changes — while coordinates
    /// are exact and were computed by the script.
    struct ShowCommand
    {
        std::string asset{};
        std::string anchor{};
        std::optional<ScreenPosition> position{};
        std::string transition{};
    };

    struct HideCommand
    {
        std::string name{};
        std::string transition{};
    };

    /// Everything the machine has to say to the world outside it.
    ///
    /// The narrow interface the whole design rests on. The machine decides what
    /// happens and says so; something else decides how that looks. Two things
    /// follow, and both are the reason the split exists: the machine can be run
    /// and tested with no window, no driver and no assets — a sink that records
    /// what it was told is a complete test double — and the same compiled script
    /// drives a headless run and a real one without knowing which it is in.
    ///
    /// It is deliberately not a general "presentation" interface. Every method is
    /// something the language can say, and adding one is a decision about the
    /// language rather than about the renderer.
    class CommandSink
    {
    public:
        CommandSink() = default;
        virtual ~CommandSink() = default;

        CommandSink(const CommandSink&) = delete;
        CommandSink& operator=(const CommandSink&) = delete;

        virtual void say(const SayCommand& command) = 0;
        virtual void offer(const MenuCommand& command) = 0;
        virtual void scene(const SceneCommand& command) = 0;
        virtual void show(const ShowCommand& command) = 0;
        virtual void hide(const HideCommand& command) = 0;
    };
}

#endif //CPEN_SCRIPT_COMMAND_SINK_HH
