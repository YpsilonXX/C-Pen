#ifndef CPEN_WINDOW_HH
#define CPEN_WINDOW_HH

#include "cpen/platform/context.hh"
#include "cpen/platform/event.hh"
#include "cpen/platform/input.hh"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// Forward-declared, not included: GLFW/glad are platform-layer implementation
/// details and must not leak into layers above (render/present/app/script).
struct GLFWwindow;

namespace cpen::platform
{
    /// Width and height in pixels.
    struct Size
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    /// A window with an OpenGL context, and the source of all input events.
    ///
    /// Requires a live Context, which owns the windowing library itself. Neither
    /// copyable nor movable: exactly one Window exists for the lifetime of the
    /// application, it registers pointers to itself with both the Context and the
    /// operating system's callbacks, and moving it would invalidate them for no
    /// benefit.
    class Window
    {
    public:
        struct Config
        {
            std::uint32_t width = 1920;
            std::uint32_t height = 1080;
            std::string title = "C-Pen";

            /// Synchronise buffer swaps with the display refresh. Prevents
            /// tearing and caps the loop at the refresh rate.
            bool vsync = true;

            /// The engine renders at a fixed virtual resolution and letterboxes
            /// it into whatever size the window happens to be, so resizing is
            /// allowed by default.
            bool resizable = true;

            /// Map the window on screen. The game always wants this; a test that
            /// needs nothing but a GL context does not, and creating the window
            /// hidden is how a context is obtained without a window flashing up
            /// for the duration of the run.
            bool visible = true;
        };

        Window(Context& context, const Config& config);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        /// True once the user or the application has requested closing.
        bool should_close() const;

        /// Sets the close flag. Lets the application end the loop itself, for
        /// instance from a quit menu entry.
        void request_close();

        /// Clears the close flag, so a "really quit?" prompt can cancel a close
        /// the user already triggered.
        void cancel_close();

        /// Events collected during the most recent Context::poll_events().
        /// The span is valid until the next poll.
        std::span<const Event> events() const;

        /// Presents the frame just rendered. Blocks until the next vertical
        /// blank when vsync is on.
        void swap_buffers();

        /// Window size in screen coordinates — the units window managers and
        /// cursor positions use.
        Size size() const;

        /// Drawable size in pixels. Differs from size() on high-DPI displays;
        /// this is the one glViewport and projection matrices need.
        Size framebuffer_size() const;

        /// Immediate keyboard state, for continuous input such as holding a
        /// direction to walk. Discrete actions ("pressed Escape") belong in the
        /// event queue, which cannot miss a press that began and ended inside
        /// one frame.
        bool is_key_down(Key key) const;

        bool is_mouse_button_down(MouseButton button) const;

        /// Cursor position in window coordinates, origin top-left.
        void cursor_position(double& x, double& y) const;

        /// Clears the event queue. Called by Context::poll_events() before the
        /// operating system's queue is drained; not part of the outward API.
        void clear_events();

    private:
        /// Appends an event, from the operating system callbacks.
        void push_event(const Event& event);

        /// Installs the callbacks and the user pointer they navigate back through.
        void install_callbacks();

        /// Operating system callbacks. Static members rather than free functions
        /// so that they can reach private state: each recovers its Window through
        /// the user pointer stored on the native handle.
        static Window& from_handle(GLFWwindow* handle);
        static void on_key(GLFWwindow* handle, int key, int scancode, int action, int modifiers);
        static void on_text(GLFWwindow* handle, unsigned int codepoint);
        /// Converts a cursor position from the screen coordinates the operating
        /// system reports into the framebuffer pixels everything above this layer
        /// works in.
        ///
        /// The two differ by the display's scaling factor, which is 1 on an
        /// ordinary monitor and anything at all on a high-DPI one. Converting
        /// here rather than in each reader is what keeps a single coordinate
        /// space in the engine: a click and the viewport that has to interpret it
        /// are then measured in the same units, and a menu hit test cannot
        /// quietly answer the wrong question on somebody else's screen.
        static void to_framebuffer_pixels(GLFWwindow* handle, double& x, double& y);

        static void on_mouse_button(GLFWwindow* handle, int button, int action, int modifiers);
        static void on_cursor_position(GLFWwindow* handle, double x, double y);
        static void on_scroll(GLFWwindow* handle, double x_offset, double y_offset);
        static void on_framebuffer_size(GLFWwindow* handle, int width, int height);
        static void on_close(GLFWwindow* handle);
        static void on_focus(GLFWwindow* handle, int focused);

        Context* owning_context = nullptr;
        GLFWwindow* handle = nullptr;
        std::vector<Event> event_queue;
    };
}

#endif //CPEN_WINDOW_HH
