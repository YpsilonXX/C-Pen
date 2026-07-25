#include <cpen/core/log.hh>
#include <cpen/platform/context.hh>
#include <cpen/platform/window.hh>

#include <glad/glad.h>

#include <variant>

using namespace cpen;

namespace
{
    /// Handles one platform event. Returns false when the application should
    /// stop running.
    ///
    /// TODO(F0): this dispatch belongs in the game-state stack — an event will be
    /// offered to the top state first and travel down until something consumes it.
    bool handle_event(const platform::Event& event)
    {
        if (const auto* key = std::get_if<platform::KeyEvent>(&event))
        {
            if (key->key == platform::Key::ESCAPE && key->action == platform::InputAction::PRESS)
            {
                log::info(log::Category::APP, "escape pressed, closing");
                return false;
            }
        }
        else if (const auto* resize = std::get_if<platform::ResizeEvent>(&event))
        {
            // TODO(F1): the render layer owns the viewport once it exists; the
            // platform layer only reports the new size.
            glViewport(0, 0,
                       static_cast<GLsizei>(resize->width),
                       static_cast<GLsizei>(resize->height));
            log::debug(log::Category::APP, "framebuffer resized to {}x{}",
                       resize->width, resize->height);
        }
        else if (std::holds_alternative<platform::CloseEvent>(event))
        {
            log::info(log::Category::APP, "close requested");
            return false;
        }
        else if (const auto* focus = std::get_if<platform::FocusEvent>(&event))
        {
            log::debug(log::Category::APP, "focus {}", focus->focused ? "gained" : "lost");
        }

        return true;
    }
}

/// Entry point for the C-Pen demo game executable.
/// F0 smoke test: open a window and clear it until closed or Esc is pressed.
/// TODO(F0): replace with the real bootstrap through the game-state stack.
int main()
{
    log::initialize_console();
    log::info(log::Category::APP, "C-Pen demo starting");

    platform::Context context;
    platform::Window window(context, platform::Window::Config{});

    double previous_time = context.time();
    bool running = true;

    while (running && !window.should_close())
    {
        const double now = context.time();
        const double delta_time = now - previous_time;
        previous_time = now;

        // TODO(F0): delta_time feeds the state stack's update(); nothing consumes
        // it yet, but the loop already produces it.
        static_cast<void>(delta_time);

        context.poll_events();

        for (const platform::Event& event : window.events())
        {
            if (!handle_event(event))
            {
                running = false;
            }
        }

        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        window.swap_buffers();
    }

    log::info(log::Category::APP, "C-Pen demo shutting down");
    return 0;
}
