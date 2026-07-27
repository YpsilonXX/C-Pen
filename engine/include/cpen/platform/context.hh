#ifndef CPEN_PLATFORM_CONTEXT_HH
#define CPEN_PLATFORM_CONTEXT_HH

#include <vector>

namespace cpen::platform
{
    class Window;

    /// Owns the windowing library's lifetime for the whole process.
    ///
    /// glfwInit/glfwTerminate are global, not per-window: terminating destroys
    /// every window, context and callback at once. Tying that to a Window's
    /// destructor would mean the first window closed takes the library down with
    /// it, so ownership lives here instead. Construct exactly one Context, before
    /// any Window, and keep it alive longer than all of them.
    ///
    /// Failure to initialise is fatal — there is no meaningful way to continue a
    /// graphical application without a windowing system.
    class Context
    {
    public:
        Context();
        ~Context();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        /// Clears every attached window's event queue, then drains the operating
        /// system's queue, which invokes the callbacks that refill those queues.
        /// Clearing here rather than in the consumer means a caller cannot leak
        /// events by forgetting to reset the queue between frames.
        void poll_events();

        /// Seconds since the Context was created, monotonic and unaffected by
        /// wall-clock changes. The game loop's delta time comes from here.
        double time() const;

        /// Attaches a window to the poll cycle. Called by Window's constructor;
        /// not part of the layer's outward-facing API.
        void attach(Window& window);

        /// Detaches a window. Called by Window's destructor.
        void detach(Window& window);

    private:
        std::vector<Window*> windows;
    };
}

#endif //CPEN_PLATFORM_CONTEXT_HH
