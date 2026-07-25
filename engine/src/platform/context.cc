#include "cpen/platform/context.hh"

#include "cpen/core/log.hh"
#include "cpen/platform/window.hh"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace cpen::platform
{
    namespace
    {
        /// GLFW reports failures through this callback rather than return codes.
        /// Registered before glfwInit() so that initialisation errors are
        /// described rather than merely counted.
        void glfw_error_callback(const int code, const char* description)
        {
            log::error(log::Category::PLATFORM, "glfw error {}: {}", code, description);
        }

        /// Guards against a second Context. glfwTerminate from one instance would
        /// invalidate everything the other owns, and the failure would surface far
        /// from its cause, so it is refused outright.
        bool context_exists = false;
    }

    Context::Context()
    {
        if (context_exists)
        {
            log::fatal(log::Category::PLATFORM, "a platform::Context already exists");
        }

        glfwSetErrorCallback(glfw_error_callback);

        if (glfwInit() != GLFW_TRUE)
        {
            log::fatal(log::Category::PLATFORM, "glfwInit failed");
        }

        context_exists = true;

        int major = 0;
        int minor = 0;
        int revision = 0;
        glfwGetVersion(&major, &minor, &revision);
        log::info(log::Category::PLATFORM, "glfw {}.{}.{} initialised", major, minor, revision);
    }

    Context::~Context()
    {
        if (!this->windows.empty())
        {
            // Not fatal — the process is shutting down either way — but it means
            // windows will be destroyed by glfwTerminate behind their owners'
            // backs, which is worth knowing about.
            log::warn(log::Category::PLATFORM,
                      "{} window(s) still attached at Context destruction",
                      this->windows.size());
        }

        glfwTerminate();
        context_exists = false;
    }

    void Context::poll_events()
    {
        for (Window* window : this->windows)
        {
            window->clear_events();
        }

        glfwPollEvents();
    }

    double Context::time() const
    {
        return glfwGetTime();
    }

    void Context::attach(Window& window)
    {
        this->windows.push_back(&window);
    }

    void Context::detach(Window& window)
    {
        const auto position = std::ranges::find(this->windows, &window);
        if (position != this->windows.end())
        {
            this->windows.erase(position);
        }
    }
}
