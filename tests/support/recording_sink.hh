#ifndef CPEN_TESTS_SUPPORT_RECORDING_SINK_HH
#define CPEN_TESTS_SUPPORT_RECORDING_SINK_HH

#include "cpen/script/command_sink.hh"

#include <vector>

namespace cpen::test
{
    /// Remembers everything the machine said instead of drawing it.
    ///
    /// The whole test double the script layer needs, which is the point of the
    /// sink being as narrow as it is: a story can be run to its end, and every
    /// line, choice and sprite it produced checked, with no window, no driver and
    /// no assets anywhere in the test.
    class RecordingSink final : public script::CommandSink
    {
    public:
        void say(const script::SayCommand& command) override { this->lines.push_back(command); }
        void offer(const script::MenuCommand& command) override { this->menus.push_back(command); }
        void scene(const script::SceneCommand& command) override { this->scenes.push_back(command); }
        void show(const script::ShowCommand& command) override { this->shown.push_back(command); }
        void hide(const script::HideCommand& command) override { this->hidden.push_back(command); }

        std::vector<script::SayCommand> lines;
        std::vector<script::MenuCommand> menus;
        std::vector<script::SceneCommand> scenes;
        std::vector<script::ShowCommand> shown;
        std::vector<script::HideCommand> hidden;
    };
}

#endif //CPEN_TESTS_SUPPORT_RECORDING_SINK_HH
