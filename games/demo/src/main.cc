#include <cpen/app/application.hh>
#include <cpen/core/log.hh>
#include <cpen/render/buffer.hh>
#include <cpen/render/draw.hh>
#include <cpen/render/shader.hh>
#include <cpen/render/vertex_array.hh>
#include <cpen/runtime/game_state.hh>
#include <cpen/runtime/state_stack.hh>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstddef>
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

    constexpr std::size_t TRIANGLE_VERTEX_COUNT = 3;

    constexpr glm::vec4 BACKGROUND_COLOR{0.1f, 0.1f, 0.15f, 1.0f};

    /// F1 smoke test: an otherwise empty state that clears the window, draws a
    /// single triangle and quits on Escape. Its purposes are to prove that a state
    /// drives the engine through the stack rather than through a loop written in
    /// the game, and that the render layer can be driven from a state.
    ///
    /// Nothing here names a GL type or includes a GL header any more: the shader,
    /// the vertex data and the draw call all go through render/. What is still
    /// missing is a projection — positions are raw normalised device coordinates,
    /// so the triangle stretches with the window.
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
                render::set_viewport(0, 0,
                                     static_cast<int>(resize->width),
                                     static_cast<int>(resize->height));
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
            render::clear(BACKGROUND_COLOR);

            if (!this->shader.has_value() || !this->triangle_array.has_value())
            {
                return;
            }

            this->shader->bind();

            // Nothing needs a pulsing triangle; the uniform exists so that the
            // path from a state through Shader to the driver is exercised every
            // frame rather than only at startup.
            const auto tint = static_cast<float>(0.75 + 0.25 * std::sin(this->elapsed_time * 2.0));
            this->shader->set_uniform("tint", tint);

            render::draw_arrays(*this->triangle_array, render::Primitive::TRIANGLES,
                                TRIANGLE_VERTEX_COUNT);

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

            this->triangle_vertices = render::Buffer::vertex(TRIANGLE_VERTICES);

            this->triangle_array.emplace();
            this->triangle_array->attach(*this->triangle_vertices,
                                         render::VertexLayout{
                                             .attributes = {
                                                 {.type = render::AttributeType::FLOAT,
                                                  .component_count = 2},
                                                 {.type = render::AttributeType::FLOAT,
                                                  .component_count = 3},
                                             },
                                             .first_location = 0,
                                         });

            log::info(log::Category::RENDER, "triangle ready: array {}, buffer {}",
                      this->triangle_array->id(), this->triangle_vertices->id());
        }

        void destroy_triangle()
        {
            // Released in the reverse of the order they were created. The array
            // reads from the buffer, so it goes first — the same reason the fields
            // below are declared with the array last.
            this->triangle_array.reset();
            this->triangle_vertices.reset();
            this->shader.reset();
        }

        std::optional<render::Shader> shader;

        // Declaration order is the lifetime contract: members are destroyed in
        // reverse, so the array that reads from the buffer is torn down before the
        // buffer it reads from. VertexArray does not own its buffers, and this is
        // what stands in for that ownership.
        std::optional<render::Buffer> triangle_vertices;
        std::optional<render::VertexArray> triangle_array;

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
