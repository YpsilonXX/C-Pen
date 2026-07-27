#include <cpen/app/application.hh>
#include <cpen/core/log.hh>
#include <cpen/runtime/game_state.hh>
#include <cpen/runtime/state_stack.hh>

#include <glad/glad.h>

#include <memory>
#include <string_view>
#include <variant>

using namespace cpen;

namespace
{
    /// F0 smoke test: an otherwise empty state that clears the window and quits on
    /// Escape. Its only purpose is to prove that a state drives the engine through
    /// the stack rather than through a loop written in the game.
    ///
    /// TODO(F1): the GL calls below move into the render layer; a state will ask
    /// for a clear colour and draw sprites instead of touching OpenGL itself.
    class DemoState final : public runtime::GameState
    {
    public:
        std::string_view name() const override { return "demo"; }

        void on_enter() override
        {
            log::info(log::Category::APP, "demo state entered");
        }

        bool handle_event(const platform::Event& event) override
        {
            if (const auto* key = std::get_if<platform::KeyEvent>(&event))
            {
                if (key->key == platform::Key::ESCAPE &&
                    key->action == platform::InputAction::PRESS)
                {
                    log::info(log::Category::APP, "escape pressed, leaving the demo state");

                    // Popping the last state empties the stack, which ends the
                    // loop: quitting needs no separate channel back to the
                    // application.
                    this->stack().pop();
                    return true;
                }
            }
            else if (const auto* resize = std::get_if<platform::ResizeEvent>(&event))
            {
                glViewport(0, 0,
                           static_cast<GLsizei>(resize->width),
                           static_cast<GLsizei>(resize->height));
                log::debug(log::Category::APP, "framebuffer resized to {}x{}",
                           resize->width, resize->height);
                return true;
            }

            return false;
        }

        void render() override
        {
            glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    };
}

/// Entry point for the C-Pen demo game executable.
int main()
{
    log::initialize_console();
    log::info(log::Category::APP, "C-Pen demo starting");

    app::Application application;
    application.states().push(std::make_unique<DemoState>());
    application.run();

    log::info(log::Category::APP, "C-Pen demo shutting down");
    return 0;
}
