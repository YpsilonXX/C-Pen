#include "cpen/platform/window.hh"

#include "cpen/core/log.hh"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace cpen::platform
{
    namespace
    {
        /// The Key, MouseButton and InputAction enumerators mirror GLFW's values
        /// so that translation is a static_cast. These assertions turn a mistake
        /// in that table into a compile error instead of a wrong key at runtime.
        static_assert(static_cast<int>(Key::UNKNOWN) == GLFW_KEY_UNKNOWN);
        static_assert(static_cast<int>(Key::SPACE) == GLFW_KEY_SPACE);
        static_assert(static_cast<int>(Key::APOSTROPHE) == GLFW_KEY_APOSTROPHE);
        static_assert(static_cast<int>(Key::COMMA) == GLFW_KEY_COMMA);
        static_assert(static_cast<int>(Key::MINUS) == GLFW_KEY_MINUS);
        static_assert(static_cast<int>(Key::PERIOD) == GLFW_KEY_PERIOD);
        static_assert(static_cast<int>(Key::SLASH) == GLFW_KEY_SLASH);
        static_assert(static_cast<int>(Key::DIGIT_0) == GLFW_KEY_0);
        static_assert(static_cast<int>(Key::DIGIT_9) == GLFW_KEY_9);
        static_assert(static_cast<int>(Key::SEMICOLON) == GLFW_KEY_SEMICOLON);
        static_assert(static_cast<int>(Key::EQUAL) == GLFW_KEY_EQUAL);
        static_assert(static_cast<int>(Key::A) == GLFW_KEY_A);
        static_assert(static_cast<int>(Key::Z) == GLFW_KEY_Z);
        static_assert(static_cast<int>(Key::LEFT_BRACKET) == GLFW_KEY_LEFT_BRACKET);
        static_assert(static_cast<int>(Key::BACKSLASH) == GLFW_KEY_BACKSLASH);
        static_assert(static_cast<int>(Key::RIGHT_BRACKET) == GLFW_KEY_RIGHT_BRACKET);
        static_assert(static_cast<int>(Key::GRAVE_ACCENT) == GLFW_KEY_GRAVE_ACCENT);
        static_assert(static_cast<int>(Key::ESCAPE) == GLFW_KEY_ESCAPE);
        static_assert(static_cast<int>(Key::ENTER) == GLFW_KEY_ENTER);
        static_assert(static_cast<int>(Key::TAB) == GLFW_KEY_TAB);
        static_assert(static_cast<int>(Key::BACKSPACE) == GLFW_KEY_BACKSPACE);
        static_assert(static_cast<int>(Key::INSERT) == GLFW_KEY_INSERT);
        static_assert(static_cast<int>(Key::DELETE) == GLFW_KEY_DELETE);
        static_assert(static_cast<int>(Key::RIGHT) == GLFW_KEY_RIGHT);
        static_assert(static_cast<int>(Key::LEFT) == GLFW_KEY_LEFT);
        static_assert(static_cast<int>(Key::DOWN) == GLFW_KEY_DOWN);
        static_assert(static_cast<int>(Key::UP) == GLFW_KEY_UP);
        static_assert(static_cast<int>(Key::PAGE_UP) == GLFW_KEY_PAGE_UP);
        static_assert(static_cast<int>(Key::PAGE_DOWN) == GLFW_KEY_PAGE_DOWN);
        static_assert(static_cast<int>(Key::HOME) == GLFW_KEY_HOME);
        static_assert(static_cast<int>(Key::END) == GLFW_KEY_END);
        static_assert(static_cast<int>(Key::CAPS_LOCK) == GLFW_KEY_CAPS_LOCK);
        static_assert(static_cast<int>(Key::SCROLL_LOCK) == GLFW_KEY_SCROLL_LOCK);
        static_assert(static_cast<int>(Key::NUM_LOCK) == GLFW_KEY_NUM_LOCK);
        static_assert(static_cast<int>(Key::PRINT_SCREEN) == GLFW_KEY_PRINT_SCREEN);
        static_assert(static_cast<int>(Key::PAUSE) == GLFW_KEY_PAUSE);
        static_assert(static_cast<int>(Key::F1) == GLFW_KEY_F1);
        static_assert(static_cast<int>(Key::F12) == GLFW_KEY_F12);
        static_assert(static_cast<int>(Key::KEYPAD_0) == GLFW_KEY_KP_0);
        static_assert(static_cast<int>(Key::KEYPAD_9) == GLFW_KEY_KP_9);
        static_assert(static_cast<int>(Key::KEYPAD_DECIMAL) == GLFW_KEY_KP_DECIMAL);
        static_assert(static_cast<int>(Key::KEYPAD_DIVIDE) == GLFW_KEY_KP_DIVIDE);
        static_assert(static_cast<int>(Key::KEYPAD_MULTIPLY) == GLFW_KEY_KP_MULTIPLY);
        static_assert(static_cast<int>(Key::KEYPAD_SUBTRACT) == GLFW_KEY_KP_SUBTRACT);
        static_assert(static_cast<int>(Key::KEYPAD_ADD) == GLFW_KEY_KP_ADD);
        static_assert(static_cast<int>(Key::KEYPAD_ENTER) == GLFW_KEY_KP_ENTER);
        static_assert(static_cast<int>(Key::KEYPAD_EQUAL) == GLFW_KEY_KP_EQUAL);
        static_assert(static_cast<int>(Key::LEFT_SHIFT) == GLFW_KEY_LEFT_SHIFT);
        static_assert(static_cast<int>(Key::LEFT_CONTROL) == GLFW_KEY_LEFT_CONTROL);
        static_assert(static_cast<int>(Key::LEFT_ALT) == GLFW_KEY_LEFT_ALT);
        static_assert(static_cast<int>(Key::LEFT_SUPER) == GLFW_KEY_LEFT_SUPER);
        static_assert(static_cast<int>(Key::RIGHT_SHIFT) == GLFW_KEY_RIGHT_SHIFT);
        static_assert(static_cast<int>(Key::RIGHT_CONTROL) == GLFW_KEY_RIGHT_CONTROL);
        static_assert(static_cast<int>(Key::RIGHT_ALT) == GLFW_KEY_RIGHT_ALT);
        static_assert(static_cast<int>(Key::RIGHT_SUPER) == GLFW_KEY_RIGHT_SUPER);
        static_assert(static_cast<int>(Key::MENU) == GLFW_KEY_MENU);

        static_assert(static_cast<int>(MouseButton::LEFT) == GLFW_MOUSE_BUTTON_LEFT);
        static_assert(static_cast<int>(MouseButton::RIGHT) == GLFW_MOUSE_BUTTON_RIGHT);
        static_assert(static_cast<int>(MouseButton::MIDDLE) == GLFW_MOUSE_BUTTON_MIDDLE);
        static_assert(static_cast<int>(MouseButton::BUTTON_8) == GLFW_MOUSE_BUTTON_8);

        static_assert(static_cast<int>(InputAction::RELEASE) == GLFW_RELEASE);
        static_assert(static_cast<int>(InputAction::PRESS) == GLFW_PRESS);
        static_assert(static_cast<int>(InputAction::REPEAT) == GLFW_REPEAT);

        /// Unpacks GLFW's modifier bitmask into named flags.
        Modifiers to_modifiers(const int mask) noexcept
        {
            return Modifiers{
                .shift = (mask & GLFW_MOD_SHIFT) != 0,
                .control = (mask & GLFW_MOD_CONTROL) != 0,
                .alt = (mask & GLFW_MOD_ALT) != 0,
                .super = (mask & GLFW_MOD_SUPER) != 0,
                .caps_lock = (mask & GLFW_MOD_CAPS_LOCK) != 0,
                .num_lock = (mask & GLFW_MOD_NUM_LOCK) != 0,
            };
        }

        /// Clamps a signed dimension reported by the operating system. A window
        /// minimised on Windows reports a framebuffer of 0x0, which must not be
        /// mistaken for a negative size.
        std::uint32_t to_extent(const int value) noexcept
        {
            return value > 0 ? static_cast<std::uint32_t>(value) : 0u;
        }
    }

    /// Window and context creation is treated as fatal init: on failure this
    /// aborts rather than returning std::expected, since there is nothing
    /// recoverable to do without a GL context.
    Window::Window(Context& context, const Config& config)
        : owning_context(&context)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);

        this->handle = glfwCreateWindow(
            static_cast<int>(config.width),
            static_cast<int>(config.height),
            config.title.c_str(),
            nullptr,
            nullptr);

        if (this->handle == nullptr)
        {
            log::fatal(log::Category::PLATFORM, "glfwCreateWindow failed ({}x{})",
                       config.width, config.height);
        }

        // The context must be current before glad resolves GL entry points.
        glfwMakeContextCurrent(this->handle);

        if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
        {
            glfwDestroyWindow(this->handle);
            log::fatal(log::Category::PLATFORM, "gladLoadGLLoader failed");
        }

        glfwSwapInterval(config.vsync ? 1 : 0);

        this->install_callbacks();
        this->owning_context->attach(*this);

        const Size framebuffer = this->framebuffer_size();
        log::info(log::Category::PLATFORM,
                  "window \"{}\" {}x{} (framebuffer {}x{}), vsync {}, resizable {}",
                  config.title, config.width, config.height,
                  framebuffer.width, framebuffer.height,
                  config.vsync ? "on" : "off",
                  config.resizable ? "yes" : "no");
        log::info(log::Category::PLATFORM, "GL {} on {}",
                  reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                  reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    }

    Window::~Window()
    {
        this->owning_context->detach(*this);

        if (this->handle != nullptr)
        {
            glfwDestroyWindow(this->handle);
        }
    }

    void Window::install_callbacks()
    {
        // Every callback receives only the native handle, so the way back to this
        // object is the user pointer stored on it.
        glfwSetWindowUserPointer(this->handle, this);

        glfwSetKeyCallback(this->handle, &Window::on_key);
        glfwSetCharCallback(this->handle, &Window::on_text);
        glfwSetMouseButtonCallback(this->handle, &Window::on_mouse_button);
        glfwSetCursorPosCallback(this->handle, &Window::on_cursor_position);
        glfwSetScrollCallback(this->handle, &Window::on_scroll);
        glfwSetFramebufferSizeCallback(this->handle, &Window::on_framebuffer_size);
        glfwSetWindowCloseCallback(this->handle, &Window::on_close);
        glfwSetWindowFocusCallback(this->handle, &Window::on_focus);
    }

    Window& Window::from_handle(GLFWwindow* handle)
    {
        return *static_cast<Window*>(glfwGetWindowUserPointer(handle));
    }

    void Window::on_key(GLFWwindow* handle, const int key, const int scancode,
                        const int action, const int modifiers)
    {
        from_handle(handle).push_event(KeyEvent{
            .key = static_cast<Key>(key),
            .action = static_cast<InputAction>(action),
            .modifiers = to_modifiers(modifiers),
            .scancode = scancode,
        });
    }

    void Window::on_text(GLFWwindow* handle, const unsigned int codepoint)
    {
        from_handle(handle).push_event(TextEvent{
            .codepoint = static_cast<char32_t>(codepoint),
        });
    }

    void Window::on_mouse_button(GLFWwindow* handle, const int button,
                                 const int action, const int modifiers)
    {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(handle, &x, &y);

        from_handle(handle).push_event(MouseButtonEvent{
            .button = static_cast<MouseButton>(button),
            .action = static_cast<InputAction>(action),
            .modifiers = to_modifiers(modifiers),
            .x = x,
            .y = y,
        });
    }

    void Window::on_cursor_position(GLFWwindow* handle, const double x, const double y)
    {
        from_handle(handle).push_event(MouseMoveEvent{.x = x, .y = y});
    }

    void Window::on_scroll(GLFWwindow* handle, const double x_offset, const double y_offset)
    {
        from_handle(handle).push_event(ScrollEvent{.x_offset = x_offset, .y_offset = y_offset});
    }

    void Window::on_framebuffer_size(GLFWwindow* handle, const int width, const int height)
    {
        from_handle(handle).push_event(ResizeEvent{
            .width = to_extent(width),
            .height = to_extent(height),
        });
    }

    void Window::on_close(GLFWwindow* handle)
    {
        from_handle(handle).push_event(CloseEvent{});
    }

    void Window::on_focus(GLFWwindow* handle, const int focused)
    {
        from_handle(handle).push_event(FocusEvent{.focused = focused == GLFW_TRUE});
    }

    void Window::push_event(const Event& event)
    {
        this->event_queue.push_back(event);
    }

    void Window::clear_events()
    {
        // clear() keeps the capacity, so after the first few frames the queue
        // stops allocating altogether.
        this->event_queue.clear();
    }

    std::span<const Event> Window::events() const
    {
        return std::span<const Event>{this->event_queue};
    }

    bool Window::should_close() const
    {
        return glfwWindowShouldClose(this->handle) == GLFW_TRUE;
    }

    void Window::request_close()
    {
        glfwSetWindowShouldClose(this->handle, GLFW_TRUE);
    }

    void Window::cancel_close()
    {
        glfwSetWindowShouldClose(this->handle, GLFW_FALSE);
    }

    void Window::swap_buffers()
    {
        glfwSwapBuffers(this->handle);
    }

    Size Window::size() const
    {
        int width = 0;
        int height = 0;
        glfwGetWindowSize(this->handle, &width, &height);
        return Size{.width = to_extent(width), .height = to_extent(height)};
    }

    Size Window::framebuffer_size() const
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(this->handle, &width, &height);
        return Size{.width = to_extent(width), .height = to_extent(height)};
    }

    bool Window::is_key_down(const Key key) const
    {
        return glfwGetKey(this->handle, static_cast<int>(key)) == GLFW_PRESS;
    }

    bool Window::is_mouse_button_down(const MouseButton button) const
    {
        return glfwGetMouseButton(this->handle, static_cast<int>(button)) == GLFW_PRESS;
    }

    void Window::cursor_position(double& x, double& y) const
    {
        glfwGetCursorPos(this->handle, &x, &y);
    }
}
