#include <cpen/app/application.hh>
#include <cpen/core/log.hh>
#include <cpen/render/buffer.hh>
#include <cpen/render/draw.hh>
#include <cpen/render/image.hh>
#include <cpen/render/pixel_format.hh>
#include <cpen/render/shader.hh>
#include <cpen/render/texture.hh>
#include <cpen/render/vertex_array.hh>
#include <cpen/render/viewport.hh>
#include <cpen/runtime/game_context.hh>
#include <cpen/runtime/game_state.hh>
#include <cpen/runtime/state_stack.hh>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

using namespace cpen;

namespace
{
    /// Positions arrive in virtual pixels — the 1920x1080 space the game is
    /// authored in — and the projection supplied by the engine's viewport carries
    /// them into clip space. The uniform is written once at creation rather than
    /// per frame: letterboxing changes which part of the window is drawn into, and
    /// never the coordinate system drawn in.
    constexpr const char* QUAD_VERTEX_SHADER = R"(#version 330 core
layout (location = 0) in vec2 in_position;
layout (location = 1) in vec2 in_texture_coordinate;

uniform mat4 projection;

out vec2 texture_coordinate;

void main()
{
    texture_coordinate = in_texture_coordinate;
    gl_Position = projection * vec4(in_position, 0.0, 1.0);
}
)";

    /// The tint is applied to the sampled colour rather than replacing it, so that
    /// a frame in which the texture failed to load is obviously black rather than
    /// merely dim.
    constexpr const char* QUAD_FRAGMENT_SHADER = R"(#version 330 core
in vec2 texture_coordinate;

uniform sampler2D source;
uniform float tint;

out vec4 fragment_color;

void main()
{
    fragment_color = texture(source, texture_coordinate) * vec4(vec3(tint), 1.0);
}
)";

    /// The texture unit the quad's sampler reads from. Written into the `source`
    /// uniform as a plain integer, which is what a sampler uniform holds: not the
    /// texture's name, but the number of the unit it is bound to.
    constexpr int QUAD_TEXTURE_UNIT = 0;

    /// Interleaved vertex data: two position components followed by two texture
    /// coordinates, per vertex.
    ///
    /// Positions are in virtual pixels, with the origin at the top left and y
    /// growing downwards — a 640x640 square centred in the 1920x1080 design space.
    /// Written out rather than computed from the viewport, because the point of a
    /// fixed virtual resolution is that a position is a constant of the scene and
    /// not a function of the window.
    ///
    /// The texture coordinates put v = 0 on the *top* edge of the quad, which is
    /// the engine's convention and the reason render::Image never flips a decoded
    /// picture. GL numbers texture rows from the first one uploaded, and Image
    /// hands over the file's first row — the top of the picture — first. Mapping
    /// v = 0 to the top here is therefore what makes the image appear the right
    /// way up, at the cost of nothing at all; flipping the pixels instead would
    /// cost a copy of every asset at load time. Note that the same reasoning now
    /// applies to the positions: both axes agree, so the marker texel written at
    /// texture row zero belongs at the vertex with the smallest y.
    constexpr std::array<float, 16> QUAD_VERTICES = {
        // position          texture coordinate
         640.0f,  220.0f,    0.0f, 0.0f,   // top left
        1280.0f,  220.0f,    1.0f, 0.0f,   // top right
        1280.0f,  860.0f,    1.0f, 1.0f,   // bottom right
         640.0f,  860.0f,    0.0f, 1.0f,   // bottom left
    };

    /// Two triangles sharing the quad's leading diagonal. Indexed rather than six
    /// separate vertices, so that the demo exercises the index-buffer path as
    /// well — this is how every sprite the batch draws will be assembled.
    constexpr std::array<std::uint32_t, 6> QUAD_INDICES = {0, 1, 2, 2, 3, 0};

    constexpr std::size_t QUAD_INDEX_COUNT = QUAD_INDICES.size();

    constexpr glm::vec4 BACKGROUND_COLOR{0.1f, 0.1f, 0.15f, 1.0f};

    /// Side of the generated checkerboard, in texels.
    constexpr std::uint32_t CHECKERBOARD_SIZE = 8;

    /// Builds the demo's texture in memory rather than loading a file.
    ///
    /// The engine can decode a PNG — that is what render::Image::from_file is for —
    /// but nothing yet tells a game where its assets live, and a demo that depends
    /// on the working directory it was started from would fail for reasons that
    /// have nothing to do with the render layer. Generating the pixels keeps the
    /// repository free of binary fixtures until the asset layer of the next phase
    /// gives a file path somewhere honest to come from.
    ///
    /// The pattern is deliberately not symmetric: one corner texel is a different
    /// colour again, so that the picture on screen answers the orientation
    /// question rather than merely looking plausible. That marker must appear at
    /// the top left.
    std::vector<std::byte> generate_checkerboard()
    {
        constexpr std::array<std::uint8_t, 4> LIGHT = {230, 220, 200, 255};
        constexpr std::array<std::uint8_t, 4> DARK = {60, 70, 110, 255};
        constexpr std::array<std::uint8_t, 4> MARKER = {220, 60, 60, 255};

        std::vector<std::byte> pixels(
            render::image_size_in_bytes(CHECKERBOARD_SIZE, CHECKERBOARD_SIZE,
                                        render::PixelFormat::RGBA8));

        std::size_t offset = 0;
        for (std::uint32_t row = 0; row < CHECKERBOARD_SIZE; ++row)
        {
            for (std::uint32_t column = 0; column < CHECKERBOARD_SIZE; ++column)
            {
                // Row zero is the top row of the picture, so the marker written at
                // (0, 0) is the top-left texel.
                const bool is_marker = row == 0 && column == 0;
                const bool is_light = ((row + column) % 2) == 0;

                const std::array<std::uint8_t, 4>& color =
                    is_marker ? MARKER : (is_light ? LIGHT : DARK);

                for (const std::uint8_t channel : color)
                {
                    pixels[offset++] = static_cast<std::byte>(channel);
                }
            }
        }

        return pixels;
    }

    /// F1 smoke test: an otherwise empty state that clears the window, draws one
    /// textured quad and quits on Escape. Its purposes are to prove that a state
    /// drives the engine through the stack rather than through a loop written in
    /// the game, and that the render layer can be driven from a state.
    ///
    /// Nothing here names a GL type or includes a GL header: the shader, the
    /// vertex and index data, the texture and the draw call all go through
    /// render/. Nor does it manage the window: positions are in virtual pixels and
    /// the engine's viewport letterboxes them, so resizing the window neither
    /// distorts the quad nor requires the state to do anything.
    class DemoState final : public runtime::GameState
    {
    public:
        std::string_view name() const override { return "demo"; }

        void on_enter() override
        {
            log::info(log::Category::APP, "demo state entered");
            this->create_quad();
        }

        /// GL resources are released here rather than in the destructor because
        /// the stack calls on_exit() while the context is still current:
        /// Application declares the stack after the window, so the stack is torn
        /// down first and the context outlives every state.
        void on_exit() override
        {
            this->destroy_quad();
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
                // Nothing is done about it here any more: the Application refits
                // the viewport before the event reaches the stack, and the quad's
                // coordinates are virtual, so the scene needs no adjusting at all.
                // Logged only to make that visible while running the demo.
                log::debug(log::Category::APP,
                           "framebuffer resized to {}x{}, content now {}x{} at ({}, {})",
                           resize->width, resize->height,
                           this->context().viewport.rect().width,
                           this->context().viewport.rect().height,
                           this->context().viewport.rect().x,
                           this->context().viewport.rect().y);
                return false;
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

            if (!this->shader.has_value() || !this->texture.has_value() ||
                !this->quad_array.has_value())
            {
                return;
            }

            this->shader->bind();

            // Nothing needs a pulsing quad; the uniform exists so that the path
            // from a state through Shader to the driver is exercised every frame
            // rather than only at startup.
            const auto tint = static_cast<float>(0.75 + 0.25 * std::sin(this->elapsed_time * 2.0));
            this->shader->set_uniform("tint", tint);
            this->shader->set_uniform("source", QUAD_TEXTURE_UNIT);

            this->texture->bind(QUAD_TEXTURE_UNIT);

            render::draw_elements(*this->quad_array, render::Primitive::TRIANGLES,
                                  QUAD_INDEX_COUNT);

            render::Texture::unbind(QUAD_TEXTURE_UNIT);
            render::Shader::unbind();
        }

    private:
        void create_quad()
        {
            auto created_shader = render::Shader::create("demo.quad", QUAD_VERTEX_SHADER,
                                                         QUAD_FRAGMENT_SHADER);
            if (!created_shader)
            {
                // Reported, not fatal: the window stays up and the diagnostic
                // stays readable, which is the whole point of routing this through
                // std::expected instead of aborting.
                log::error(log::Category::RENDER, "{}", created_shader.error());
                return;
            }

            this->shader = std::move(*created_shader);

            // Written once, not per frame. The projection is a property of the
            // virtual resolution, which never changes, and a uniform's value lives
            // in the program object rather than in the context — so this survives
            // every later bind.
            this->shader->bind();
            this->shader->set_uniform("projection", this->context().viewport.projection());
            render::Shader::unbind();

            // Nearest filtering, because the checkerboard is eight texels across
            // and stretched over most of the window: linear filtering would blur
            // every cell boundary into a gradient and hide exactly what the
            // texture is here to show.
            // Named rather than passed inline: a std::span built from a temporary
            // vector stays valid only to the end of the full expression, which is
            // true here but is not a rule worth relying on in passing.
            const std::vector<std::byte> checkerboard = generate_checkerboard();

            auto created_texture = render::Texture::from_pixels(
                checkerboard, CHECKERBOARD_SIZE, CHECKERBOARD_SIZE,
                render::PixelFormat::RGBA8,
                render::TextureConfig{
                    .minify_filter = render::TextureFilter::NEAREST,
                    .magnify_filter = render::TextureFilter::NEAREST,
                });
            if (!created_texture)
            {
                log::error(log::Category::RENDER, "{}", created_texture.error());
                return;
            }

            this->texture = std::move(*created_texture);

            this->quad_vertices = render::Buffer::vertex(QUAD_VERTICES);
            this->quad_indices = render::Buffer::index(QUAD_INDICES);

            this->quad_array.emplace();
            this->quad_array->attach(*this->quad_vertices,
                                     render::VertexLayout{
                                         .attributes = {
                                             {.type = render::AttributeType::FLOAT,
                                              .component_count = 2},
                                             {.type = render::AttributeType::FLOAT,
                                              .component_count = 2},
                                         },
                                         .first_location = 0,
                                     });
            this->quad_array->set_index_buffer(*this->quad_indices);

            log::info(log::Category::RENDER,
                      "quad ready: array {}, vertices {}, indices {}, texture {}",
                      this->quad_array->id(), this->quad_vertices->id(),
                      this->quad_indices->id(), this->texture->id());
        }

        void destroy_quad()
        {
            // Released in the reverse of the order they were created. The array
            // reads from the buffers, so it goes first — the same reason the
            // fields below are declared with the array last.
            this->quad_array.reset();
            this->quad_indices.reset();
            this->quad_vertices.reset();
            this->texture.reset();
            this->shader.reset();
        }

        std::optional<render::Shader> shader;
        std::optional<render::Texture> texture;

        // Declaration order is the lifetime contract: members are destroyed in
        // reverse, so the array that reads from the buffers is torn down before
        // the buffers it reads from. VertexArray does not own its buffers, and
        // this is what stands in for that ownership.
        std::optional<render::Buffer> quad_vertices;
        std::optional<render::Buffer> quad_indices;
        std::optional<render::VertexArray> quad_array;

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
