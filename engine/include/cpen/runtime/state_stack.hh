#ifndef CPEN_RUNTIME_STATE_STACK_HH
#define CPEN_RUNTIME_STATE_STACK_HH

#include "cpen/platform/event.hh"
#include "cpen/runtime/game_state.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cpen::runtime
{
    struct GameContext;

    /// Pushdown stack of game states, and the traversal rules over it.
    ///
    /// Structural changes are queued rather than applied where they are requested:
    /// a state that popped itself in the middle of update() would be destroyed
    /// while its own method was still running. push/pop/replace therefore record a
    /// command, and apply_pending() enacts them at the loop's synchronisation
    /// point — the same discipline the event bus follows, for the same reason.
    ///
    /// Traversal order differs per operation: input and update run top-down, since
    /// the topmost state has priority and may shield the rest; rendering runs
    /// bottom-up from the deepest visible state, so what is on top is drawn last.
    class StateStack
    {
    public:
        /// Upper bound on generations of queued commands enacted by one
        /// apply_pending() call. States entered by a command may queue further
        /// commands; a cycle is reported instead of looping forever.
        static constexpr unsigned MAX_APPLY_PASSES = 8;

        explicit StateStack(GameContext& game_context) noexcept
            : services(&game_context)
        {
        }

        /// Exits and destroys whatever is still on the stack, top first.
        ///
        /// Since on_exit() runs here, everything the states reach into must
        /// outlive the stack. Owners therefore declare their StateStack last, so
        /// that reverse destruction order takes it down before the services it
        /// hands out — Application does exactly that.
        ~StateStack();

        StateStack(const StateStack&) = delete;
        StateStack& operator=(const StateStack&) = delete;

        /// Queues a push. The state is constructed by the caller and adopted here;
        /// it is attached and entered when the command is applied.
        void push(std::unique_ptr<GameState> state);

        /// Queues removal of the topmost state.
        void pop();

        /// Queues "pop the top, then push this one". The state underneath stays
        /// covered throughout, so it is neither resumed nor paused again.
        void replace(std::unique_ptr<GameState> state);

        /// Queues removal of every state, top first.
        void clear();

        /// Enacts queued commands. Called once per frame, after events and update
        /// have been delivered and before rendering.
        void apply_pending();

        /// Offers the event to the states top-down, stopping at the first that
        /// consumes it or that blocks input to those below.
        void handle_event(const platform::Event& event);

        /// Updates top-down, stopping after the first state that blocks updates
        /// below it.
        void update(double delta_time);

        /// Draws bottom-up, starting at the deepest state still visible.
        void render();

        bool empty() const noexcept { return this->states.empty(); }
        std::size_t size() const noexcept { return this->states.size(); }
        bool has_pending() const noexcept { return !this->commands.empty(); }

        /// Topmost state, or nullptr when the stack is empty.
        GameState* top() const noexcept
        {
            return this->states.empty() ? nullptr : this->states.back().get();
        }

    private:
        struct Command
        {
            enum class Kind : std::uint8_t
            {
                PUSH,
                POP,
                REPLACE,
                CLEAR,
            };

            Kind kind = Kind::POP;
            std::unique_ptr<GameState> state;
        };

        void apply(Command command);
        void enter(std::unique_ptr<GameState> state);
        void leave();

        /// Index of the deepest state that has to be drawn: everything below the
        /// topmost blocks_render_below() is hidden.
        std::size_t lowest_visible() const noexcept;

        GameContext* services = nullptr;
        std::vector<std::unique_ptr<GameState>> states;
        std::vector<Command> commands;
    };
}

#endif //CPEN_RUNTIME_STATE_STACK_HH
