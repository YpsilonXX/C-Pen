#ifndef CPEN_APP_APPLICATION_HH
#define CPEN_APP_APPLICATION_HH

#include "cpen/app/asset_roots.hh"
#include "cpen/assets/asset_manager.hh"
#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/blackboard.hh"
#include "cpen/core/event_bus.hh"
#include "cpen/platform/context.hh"
#include "cpen/platform/window.hh"
#include "cpen/render/renderer.hh"
#include "cpen/runtime/game_context.hh"
#include "cpen/runtime/state_stack.hh"

#include <optional>

namespace cpen::app
{
    /// Owns the engine's services and runs the frame loop.
    ///
    /// Deliberately thin: it creates the window, the shared services and the state
    /// stack, then does nothing per frame except call them in a fixed order. All
    /// behaviour belongs to the states. This is the counterweight to the previous
    /// engine, where a single god object accumulated the whole game.
    class Application
    {
    public:
        struct Config
        {
            platform::Window::Config window{};

            /// The resolution the game positions everything in, independent of the
            /// window it ends up in. Separate from the window size above: that one
            /// is only the size the window opens at and the user may change it at
            /// any moment, whereas this one is a property of the artwork and never
            /// changes at all.
            std::uint32_t virtual_width = render::Viewport::DEFAULT_VIRTUAL_WIDTH;
            std::uint32_t virtual_height = render::Viewport::DEFAULT_VIRTUAL_HEIGHT;
            render::ScaleMode scale_mode = render::ScaleMode::LETTERBOX;

            /// Sprites the frame's batch holds before it has to flush.
            std::size_t sprite_capacity = render::SpriteBatch::DEFAULT_CAPACITY;

            /// Where the game's files and the engine's files are.
            ///
            /// Empty means the layout beside the executable, which is what a game
            /// started normally wants. A main() that reads a command line puts the
            /// result of asset_roots_from_command_line here instead.
            ///
            /// An optional rather than a filled-in default because working the
            /// default out means asking the operating system where the program is,
            /// and that is not something a plain struct should be doing when it is
            /// declared.
            std::optional<AssetRoots> roots;

            /// Upper bound on one frame's delta time, in seconds.
            ///
            /// After a breakpoint, a drag of the window or a machine waking from
            /// sleep, the true delta can be seconds long; feeding that to the
            /// states would teleport every animation and, if updates are expensive,
            /// make each frame produce an even larger delta. Clamping trades exact
            /// wall-clock fidelity for a loop that always recovers.
            double maximum_delta_time = 0.25;
        };

        /// Two constructors rather than one with `Config configuration = {}`: a
        /// nested class's default member initialisers are not available to
        /// declarations in the enclosing class, so that default argument does not
        /// compile. The delegating default constructor is defined out of line,
        /// where Config is complete.
        Application();
        explicit Application(Config settings);

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        /// Push the initial state here before calling run().
        runtime::StateStack& states() noexcept { return this->stack; }

        runtime::GameContext& context() noexcept { return this->game_context; }
        platform::Window& window() noexcept { return this->main_window; }
        render::Renderer& renderer() noexcept { return this->main_renderer; }

        assets::AssetManager& assets() noexcept { return this->asset_manager; }
        assets::VirtualFileSystem& files() noexcept { return this->file_system; }

        /// Exposed so that a game can register aliases before it starts — the
        /// manifest reader will do the same thing when there is one.
        assets::AssetResolver& resolver() noexcept { return this->asset_resolver; }

        /// Runs until the window is closed, the state stack empties, or
        /// request_quit() is called. Returns once the loop has ended; the states
        /// are torn down with the Application.
        void run();

        /// Ends the loop after the current frame.
        void request_quit() noexcept { this->running = false; }

    private:
        void route_events();

        /// Refits the renderer's viewport to the current framebuffer size and hands
        /// the resulting rectangle to GL.
        ///
        /// Called once during construction and again on every resize. The initial
        /// call is the point: GL's own default viewport is whatever the framebuffer
        /// happened to be when the context was made current, which is right only
        /// until the letterbox bars are needed — and nothing below this class can
        /// make the call, because no state has a window to ask for the size.
        void refit_viewport();

        /// Builds the renderer, falling back to one that cannot draw if its shader
        /// will not compile.
        ///
        /// Reported and survived rather than fatal, as everywhere else a driver
        /// refuses something: a window that stays up with a readable diagnostic in
        /// it beats a process that vanished before the message could be read.
        static render::Renderer make_renderer(const Config& settings);

        /// Mounts the game's root and then the engine's, in that order, so that a
        /// game's file replaces an engine file of the same name.
        void mount_roots();

        /// Prints what went wrong with the game's files, once, as the loop ends.
        ///
        /// Both reports are about mistakes that are invisible while playing: an
        /// asset that never loaded left a hole somebody may not have looked at,
        /// and a name that differs from the file only in case works here and fails
        /// on another machine entirely. Neither survives being one line in a long
        /// run, so they are repeated where a run ends and a person is looking.
        void report_asset_problems() const;

        Config configuration;

        // Declaration order is construction order, and every line below depends on
        // the ones above it: the window needs the platform context, the renderer
        // compiles a shader and so needs that context current, the blackboard
        // publishes to the bus, the game context refers to them, and the stack
        // hands that context to every state.
        platform::Context platform_context;
        platform::Window main_window;
        render::Renderer main_renderer;
        core::EventBus event_bus;
        core::Blackboard blackboard;

        // No GL is involved in any of the three, which is what lets a GameContext
        // exist in a test with no window: the manager opens files and only touches
        // the driver when a texture or a typeface is actually asked for.
        assets::VirtualFileSystem file_system;
        assets::AssetResolver asset_resolver;
        assets::AssetManager asset_manager;

        runtime::GameContext game_context;
        runtime::StateStack stack;

        bool running = true;
    };
}

#endif //CPEN_APP_APPLICATION_HH
