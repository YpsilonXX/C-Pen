#ifndef CPEN_RUNTIME_DIALOGUE_STATE_HH
#define CPEN_RUNTIME_DIALOGUE_STATE_HH

#include "cpen/assets/asset_manager.hh"
#include "cpen/platform/event.hh"
#include "cpen/present/layout.hh"
#include "cpen/present/stage.hh"
#include "cpen/present/stage_view.hh"
#include "cpen/runtime/game_state.hh"
#include "cpen/script/virtual_machine.hh"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace cpen::runtime
{
    /// Telling a story: the state that runs a compiled script and puts what it
    /// says on screen.
    ///
    /// One activity among the peers on the stack rather than the engine's centre —
    /// exploration and mini-games will sit beside it, not under it. What it owns
    /// is the loop between the two halves of the script layer: the machine says
    /// what happens, the stage records it, the view draws it, and the reader's
    /// input is what resumes the machine.
    ///
    /// The machine never blocks, so neither does this: every frame runs the script
    /// as far as its next wait and then does nothing until a click, a key or a
    /// timer answers it. That is what makes a running story something a save file
    /// will be able to describe.
    ///
    /// Everything except drawing works without a graphics context: with a renderer
    /// that cannot draw, no typeface and no picture is asked for and render() does
    /// nothing, which lets a whole story be played through in a test.
    class DialogueState final : public GameState
    {
    public:
        struct Config
        {
            /// The script to play, as an asset identifier — "intro", not a path.
            std::string script{};

            /// The label to begin at. Empty starts at the top of the file, where
            /// the statements outside every label are.
            std::string label{};

            present::DialogueTheme theme{};

            /// Code points a line types itself out at. Zero puts each line up
            /// whole, which is a legitimate taste as well as what a test wants.
            double reveal_speed = present::Stage::DEFAULT_REVEAL_SPEED;
        };

        explicit DialogueState(Config settings);

        std::string_view name() const override { return "dialogue"; }

        void on_enter() override;
        void on_exit() override;

        bool handle_event(const platform::Event& event) override;
        void update(double delta_time) override;
        void render() override;

        /// What is on stage. Exposed for tests and for whatever later reads a
        /// running story to save it.
        const present::Stage& stage() const noexcept { return this->current_stage; }

        /// The machine, once the script has been loaded. Null if it never was.
        const script::VirtualMachine* machine() const noexcept
        {
            return this->virtual_machine.has_value() ? &*this->virtual_machine : nullptr;
        }

        /// The message on the failure screen, empty while the story is running.
        const std::string& failure() const noexcept { return this->failure_message; }

        /// The choice under the pointer, or the one the arrow keys have moved to.
        std::optional<std::size_t> highlighted_choice() const noexcept
        {
            return this->highlighted;
        }

    private:
        /// Runs the script to its next wait and acts on why it stopped.
        void pump();

        /// The reader went on: finishes the line if it is still typing itself out,
        /// and otherwise resumes the machine. Returns true if it did either.
        bool advance();

        bool choose(std::size_t index);

        /// Moves the highlight by `step` choices, wrapping at both ends.
        void move_highlight(int step);

        bool handle_key(const platform::KeyEvent& key);
        bool handle_click(const platform::MouseButtonEvent& click);

        /// Stops the story and puts `text` on screen. Everything after this is a
        /// failure screen and a log line; nothing resumes.
        void fail(std::string text);

        /// The reference screen, which is the space every position is in.
        glm::vec2 screen() const;

        /// A framebuffer position in the space the layout is measured in.
        glm::vec2 to_virtual(double x, double y) const;

        Config configuration;

        present::Stage current_stage;

        // Declared before the machine and after nothing: the machine holds the
        // chunk, the blackboard and the stage by reference for its whole life, so
        // everything it points at is declared above it and outlives it.
        assets::ScriptReference program;

        std::optional<present::StageView> view;
        std::optional<script::VirtualMachine> virtual_machine;

        std::string failure_message;

        std::optional<std::size_t> highlighted;

        /// Seconds left of a `pause`. Meaningful only while the machine waits for
        /// one.
        double remaining_wait = 0.0;
    };
}

#endif //CPEN_RUNTIME_DIALOGUE_STATE_HH
