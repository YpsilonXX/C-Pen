#ifndef CPEN_RUNTIME_GAME_CONTEXT_HH
#define CPEN_RUNTIME_GAME_CONTEXT_HH

namespace cpen::assets
{
    class AssetManager;
}

namespace cpen::core
{
    class Blackboard;
    class EventBus;
}

namespace cpen::render
{
    class Renderer;
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

        /// Everything a state needs in order to draw, and the only route it has to
        /// the render layer. Carries the viewport, so the coordinate system is
        /// reachable as renderer.viewport() by anything that wants to convert a
        /// coordinate without drawing anything.
        ///
        /// Non-const, because drawing genuinely changes what the renderer holds.
        /// The frame around it is not the state's to open: the Application does
        /// that once, either side of the whole render pass.
        render::Renderer& renderer;

        /// Everything the game's own files are reached through: identifiers in,
        /// pictures and typefaces out. A state never opens a file, never learns
        /// where the game was installed, and never sees a path.
        ///
        /// Non-const, because loading changes what is cached — and because a state
        /// that could not load anything would have to be handed its assets by
        /// somebody who could.
        assets::AssetManager& assets;

        // Extended as the layers below appear: presentation (F3), audio (F5).
    };
}

#endif //CPEN_RUNTIME_GAME_CONTEXT_HH
