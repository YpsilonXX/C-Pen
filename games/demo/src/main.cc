#include <cpen/app/application.hh>
#include <cpen/core/log.hh>
#include <cpen/render/shader.hh>
#include <cpen/runtime/game_state.hh>
#include <cpen/runtime/state_stack.hh>

#include <glad/glad.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>

using namespace cpen;

namespace
{
    /// Positions arrive already in clip space, so no transform is applied. The
    /// projection matrix and the virtual 1920x1080 reference resolution belong to
    /// the render layer, which does not have them yet.
    ///
    /// The per-vertex colour is interpolated by the rasteriser, which makes a
    /// successful draw unmistakable: a uniform-coloured triangle could still come
    /// from a shader that ignores its inputs.
    constexpr const char* TRIANGLE_VERTEX_SHADER = R"(#version 330 core
layout (location = 0) in vec2 in_position;
layout (location = 1) in vec3 in_color;

uniform float tint;

out vec3 vertex_color;

void main()
{
    vertex_color = in_color * tint;
    gl_Position = vec4(in_position, 0.0, 1.0);
}
)";

    constexpr const char* TRIANGLE_FRAGMENT_SHADER = R"(#version 330 core
in vec3 vertex_color;

out vec4 fragment_color;

void main()
{
    fragment_color = vec4(vertex_color, 1.0);
}
)";

    /// Interleaved vertex data: two position components followed by three colour
    /// components, per vertex. Interleaving rather than one buffer per attribute
    /// is what the sprite batch will use as well, so the layout is worth
    /// rehearsing here.
    constexpr std::array<float, 15> TRIANGLE_VERTICES = {
        // position      colour
        -0.6f, -0.5f,    1.0f, 0.0f, 0.0f,
         0.6f, -0.5f,    0.0f, 1.0f, 0.0f,
         0.0f,  0.6f,    0.0f, 0.0f, 1.0f,
    };

    constexpr GLuint POSITION_ATTRIBUTE = 0;
    constexpr GLuint COLOR_ATTRIBUTE = 1;

    constexpr GLsizei VERTEX_STRIDE = static_cast<GLsizei>(5 * sizeof(float));
    constexpr std::uintptr_t POSITION_OFFSET = 0;
    constexpr std::uintptr_t COLOR_OFFSET = 2 * sizeof(float);

    /// F1 smoke test: an otherwise empty state that clears the window, draws a
    /// single triangle and quits on Escape. Its purposes are to prove that a state
    /// drives the engine through the stack rather than through a loop written in
    /// the game, and that the render layer can be driven from a state.
    ///
    /// The shader is already render::Shader. The buffers below are not yet
    /// anything — TODO(F1): render::Buffer and render::VertexArray replace them,
    /// after which this state stops naming GL types at all.
    class DemoState final : public runtime::GameState
    {
    public:
        std::string_view name() const override { return "demo"; }

        void on_enter() override
        {
            log::info(log::Category::APP, "demo state entered");
            this->create_triangle();
        }

        /// GL resources are released here rather than in the destructor because
        /// the stack calls on_exit() while the context is still current:
        /// Application declares the stack after the window, so the stack is torn
        /// down first and the context outlives every state.
        void on_exit() override
        {
            this->destroy_triangle();
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

        void update(const double delta_time) override
        {
            this->elapsed_time += delta_time;
        }

        void render() override
        {
            glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            if (!this->shader.has_value())
            {
                return;
            }

            this->shader->bind();

            // Nothing needs a pulsing triangle; the uniform exists so that the
            // path from a state through Shader to the driver is exercised every
            // frame rather than only at startup.
            const auto tint = static_cast<float>(0.75 + 0.25 * std::sin(this->elapsed_time * 2.0));
            this->shader->set_uniform("tint", tint);

            glBindVertexArray(this->vertex_array);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);

            render::Shader::unbind();
        }

    private:
        void create_triangle()
        {
            auto created = render::Shader::create("demo.triangle", TRIANGLE_VERTEX_SHADER,
                                                  TRIANGLE_FRAGMENT_SHADER);
            if (!created)
            {
                // Reported, not fatal: the window stays up and the diagnostic
                // stays readable, which is the whole point of routing this through
                // std::expected instead of aborting.
                log::error(log::Category::RENDER, "{}", created.error());
                return;
            }

            this->shader = std::move(*created);

            // The core profile has no default vertex array object, so one must be
            // bound before any attribute state can be recorded.
            glGenVertexArrays(1, &this->vertex_array);
            glBindVertexArray(this->vertex_array);

            glGenBuffers(1, &this->vertex_buffer);
            glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(sizeof(TRIANGLE_VERTICES)),
                         TRIANGLE_VERTICES.data(),
                         GL_STATIC_DRAW);

            // The attribute pointers are recorded into the bound vertex array,
            // together with the buffer they read from; the binding above is
            // therefore part of the state being captured, not a prerequisite of
            // the draw call.
            glVertexAttribPointer(POSITION_ATTRIBUTE, 2, GL_FLOAT, GL_FALSE, VERTEX_STRIDE,
                                  reinterpret_cast<const void*>(POSITION_OFFSET));
            glEnableVertexAttribArray(POSITION_ATTRIBUTE);

            glVertexAttribPointer(COLOR_ATTRIBUTE, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE,
                                  reinterpret_cast<const void*>(COLOR_OFFSET));
            glEnableVertexAttribArray(COLOR_ATTRIBUTE);

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            log::info(log::Category::RENDER, "triangle ready: array {}, buffer {}",
                      this->vertex_array, this->vertex_buffer);
        }

        void destroy_triangle()
        {
            // Deleting name 0 is defined as a no-op, so the partially constructed
            // case needs no separate handling.
            glDeleteBuffers(1, &this->vertex_buffer);
            glDeleteVertexArrays(1, &this->vertex_array);

            this->vertex_buffer = 0;
            this->vertex_array = 0;
            this->shader.reset();
        }

        std::optional<render::Shader> shader;
        GLuint vertex_array = 0;
        GLuint vertex_buffer = 0;
        double elapsed_time = 0.0;
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
