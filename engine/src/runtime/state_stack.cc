#include "cpen/runtime/state_stack.hh"

#include "cpen/core/log.hh"
#include "cpen/runtime/game_context.hh"

#include <utility>

namespace cpen::runtime
{
    StateStack::~StateStack()
    {
        // Queued commands are abandoned: the states they would have entered are
        // destroyed here without ever having run, which is correct for teardown.
        this->commands.clear();
        while (!this->states.empty())
        {
            this->leave();
        }
    }

    void StateStack::push(std::unique_ptr<GameState> state)
    {
        if (state == nullptr)
        {
            log::error(log::Category::APP, "push of a null state ignored");
            return;
        }
        this->commands.push_back(Command{.kind = Command::Kind::PUSH, .state = std::move(state)});
    }

    void StateStack::pop()
    {
        this->commands.push_back(Command{.kind = Command::Kind::POP, .state = nullptr});
    }

    void StateStack::replace(std::unique_ptr<GameState> state)
    {
        if (state == nullptr)
        {
            log::error(log::Category::APP, "replace with a null state ignored");
            return;
        }
        this->commands.push_back(Command{.kind = Command::Kind::REPLACE, .state = std::move(state)});
    }

    void StateStack::clear()
    {
        this->commands.push_back(Command{.kind = Command::Kind::CLEAR, .state = nullptr});
    }

    void StateStack::apply_pending()
    {
        for (unsigned pass = 0; !this->commands.empty(); ++pass)
        {
            if (pass == MAX_APPLY_PASSES)
            {
                log::error(log::Category::APP,
                           "state transitions did not settle after {} passes, {} command(s) deferred",
                           MAX_APPLY_PASSES, this->commands.size());
                return;
            }

            // Detached before enacting: on_enter() and on_exit() are allowed to
            // queue further transitions, and those belong to the next generation.
            std::vector<Command> batch;
            batch.swap(this->commands);

            for (Command& command : batch)
            {
                this->apply(std::move(command));
            }
        }
    }

    void StateStack::apply(Command command)
    {
        switch (command.kind)
        {
            case Command::Kind::PUSH:
                if (!this->states.empty())
                {
                    this->states.back()->on_pause();
                }
                this->enter(std::move(command.state));
                break;

            case Command::Kind::POP:
                if (this->states.empty())
                {
                    log::warn(log::Category::APP, "pop on an empty state stack ignored");
                    break;
                }
                this->leave();
                if (!this->states.empty())
                {
                    this->states.back()->on_resume();
                }
                break;

            case Command::Kind::REPLACE:
                // The state underneath stays covered from beginning to end, so it
                // is neither resumed by the removal nor paused by the arrival.
                if (!this->states.empty())
                {
                    this->leave();
                }
                this->enter(std::move(command.state));
                break;

            case Command::Kind::CLEAR:
                while (!this->states.empty())
                {
                    this->leave();
                }
                break;
        }
    }

    void StateStack::enter(std::unique_ptr<GameState> state)
    {
        GameState& entering = *state;
        entering.attach(*this, *this->services);
        this->states.push_back(std::move(state));

        log::debug(log::Category::APP, "state '{}' entered, depth {}", entering.name(),
                   this->states.size());
        entering.on_enter();
    }

    void StateStack::leave()
    {
        // Detached from the stack before on_exit() runs, so a state that reaches
        // back into the stack during teardown cannot observe itself still there.
        std::unique_ptr<GameState> leaving = std::move(this->states.back());
        this->states.pop_back();

        leaving->on_exit();
        log::debug(log::Category::APP, "state '{}' left, depth {}", leaving->name(),
                   this->states.size());
    }

    void StateStack::handle_event(const platform::Event& event)
    {
        for (std::size_t index = this->states.size(); index > 0; --index)
        {
            GameState& state = *this->states[index - 1];
            if (state.handle_event(event))
            {
                return;
            }
            if (state.blocks_input_below())
            {
                return;
            }
        }
    }

    void StateStack::update(const double delta_time)
    {
        for (std::size_t index = this->states.size(); index > 0; --index)
        {
            GameState& state = *this->states[index - 1];
            state.update(delta_time);
            if (state.blocks_update_below())
            {
                return;
            }
        }
    }

    std::size_t StateStack::lowest_visible() const noexcept
    {
        for (std::size_t index = this->states.size(); index > 0; --index)
        {
            if (this->states[index - 1]->blocks_render_below())
            {
                return index - 1;
            }
        }
        return 0;
    }

    void StateStack::render()
    {
        for (std::size_t index = this->lowest_visible(); index < this->states.size(); ++index)
        {
            this->states[index]->render();
        }
    }
}
