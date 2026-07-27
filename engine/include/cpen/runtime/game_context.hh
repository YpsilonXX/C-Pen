#ifndef CPEN_RUNTIME_GAME_CONTEXT_HH
#define CPEN_RUNTIME_GAME_CONTEXT_HH

namespace cpen::core
{
    class Blackboard;
    class EventBus;
}

namespace cpen::runtime
{
    /// The engine services every game state is given.
    ///
    /// A bundle of references rather than a set of constructor parameters: adding
    /// a service later must not touch the signature of every state, and a test can
    /// assemble a context over its own blackboard and bus without a window or a GL
    /// context existing.
    ///
    /// It holds references only. Ownership stays with the Application, and game
    /// data belongs to the states themselves — this must not grow into a place to
    /// stash whatever is convenient, which is exactly how the previous engine's
    /// god object came about.
    struct GameContext
    {
        core::Blackboard& blackboard;
        core::EventBus& event_bus;

        // Extended as the layers below appear: asset manager (F1), renderer and
        // presentation (F1-F3), audio (F5).
    };
}

#endif //CPEN_RUNTIME_GAME_CONTEXT_HH
