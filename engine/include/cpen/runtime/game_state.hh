#ifndef CPEN_RUNTIME_GAME_STATE_HH
#define CPEN_RUNTIME_GAME_STATE_HH

#include "cpen/platform/event.hh"

#include <string_view>

namespace cpen::runtime
{
    class StateStack;
    struct GameContext;

    /// One activity the game can be doing: telling a dialogue, walking around a
    /// location, playing a mini-game, showing a menu.
    ///
    /// This — not the visual-novel runtime — is the root of the engine. The VN
    /// script VM lives *inside* a dialogue state and is one peer activity among
    /// others, which is what keeps free-roam and mini-games additive instead of a
    /// rewrite. Even the F0 skeleton runs its single empty state through the stack
    /// so the shape is exercised from the first frame.
    ///
    /// A state is driven exclusively by its StateStack, which attaches it before
    /// on_enter() and never calls anything afterwards; context() and stack() are
    /// therefore always valid inside the callbacks below.
    class GameState
    {
    public:
        virtual ~GameState() = default;

        GameState() = default;
        GameState(const GameState&) = delete;
        GameState& operator=(const GameState&) = delete;

        /// Identifies the state in logs and in stack dumps.
        virtual std::string_view name() const = 0;

        /// Called once, after the state has been pushed and attached.
        virtual void on_enter() {}

        /// Called once, before the state is destroyed.
        virtual void on_exit() {}

        /// Called when another state is pushed on top of this one, and again when
        /// that state is popped. A dialogue that stops its typewriter while a
        /// menu is open does it here.
        virtual void on_pause() {}
        virtual void on_resume() {}

        /// Returns true if the event was consumed and must not travel further
        /// down the stack. Note the difference from blocks_input_below(): a modal
        /// state swallows events it does not care about as well.
        virtual bool handle_event(const platform::Event& event)
        {
            static_cast<void>(event);
            return false;
        }

        virtual void update(double delta_time)
        {
            static_cast<void>(delta_time);
        }

        virtual void render() {}

        /// Visibility and reachability of the states underneath.
        ///
        /// The defaults describe a full-screen state that entirely supersedes what
        /// it covers. An overlay — a menu drawn over a frozen dialogue — returns
        /// false from blocks_render_below() while keeping the other two true.
        virtual bool blocks_update_below() const { return true; }
        virtual bool blocks_render_below() const { return true; }
        virtual bool blocks_input_below() const { return true; }

    protected:
        /// Engine services. Valid from on_enter() onwards.
        GameContext& context() const;

        /// The owning stack, through which a state pushes a successor or pops
        /// itself. Structural changes are queued and take effect at the stack's
        /// synchronisation point, so calling these from inside update() or an
        /// event handler is safe.
        StateStack& stack() const;

    private:
        friend class StateStack;

        void attach(StateStack& owner, GameContext& attached_context) noexcept
        {
            this->owning_stack = &owner;
            this->services = &attached_context;
        }

        StateStack* owning_stack = nullptr;
        GameContext* services = nullptr;
    };
}

#endif //CPEN_RUNTIME_GAME_STATE_HH
