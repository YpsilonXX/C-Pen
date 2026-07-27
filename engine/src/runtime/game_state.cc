#include "cpen/runtime/game_state.hh"

#include "cpen/core/log.hh"
#include "cpen/runtime/game_context.hh"
#include "cpen/runtime/state_stack.hh"

namespace cpen::runtime
{
    GameContext& GameState::context() const
    {
        if (this->services == nullptr)
        {
            // Reachable only by calling a state's own methods before the stack has
            // adopted it, which no engine code does.
            log::fatal(log::Category::APP, "state '{}' used its context before being attached",
                       this->name());
        }
        return *this->services;
    }

    StateStack& GameState::stack() const
    {
        if (this->owning_stack == nullptr)
        {
            log::fatal(log::Category::APP, "state '{}' used its stack before being attached",
                       this->name());
        }
        return *this->owning_stack;
    }
}
