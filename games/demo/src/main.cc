#include <cpen/app/application.hh>
#include <cpen/core/log.hh>
#include <cpen/render/draw.hh>
#include <cpen/render/image.hh>
#include <cpen/render/pixel_format.hh>
#include <cpen/render/sprite.hh>
#include <cpen/render/sprite_batch.hh>
#include <cpen/render/texture.hh>
#include <cpen/render/viewport.hh>
#include <cpen/runtime/game_context.hh>
#include <cpen/runtime/game_state.hh>

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

    /// F1 smoke test: an otherwise empty state that clears the window, draws a few
    /// sprites through the batch and quits on Escape. Its purposes are to prove
    /// that a state drives the engine through the stack rather than through a loop
    /// written in the game, and that the render layer can be driven from a state.
    ///
    /// Nothing here names a GL type, includes a GL header, compiles a shader or
    /// touches a buffer: a scene is a texture and a handful of Sprite values, and
    /// the batch turns the four of them below into one draw call because they share
    /// a texture. Nor does the state manage the window — positions are in virtual
    /// pixels and the engine's viewport letterboxes them, so resizing distorts
    /// nothing and requires nothing.
    class DemoState final : public runtime::GameState
    {
    public:
        std::string_view name() const override { return "demo"; }

        void on_enter() override
        {
            log::info(log::Category::APP, "demo state entered");
            this->create_scene();
        }

        /// GL resources are released here rather than in the destructor because
        /// the stack calls on_exit() while the context is still current:
        /// Application declares the stack after the window, so the stack is torn
        /// down first and the context outlives every state.
        void on_exit() override
        {
            this->batch.reset();
            this->texture.reset();
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
                // Nothing is done about it here: the Application refits the viewport
                // before the event reaches the stack, and the sprites are positioned
                // in virtual pixels, so the scene needs no adjusting at all. Logged
                // only to make that visible while running the demo.
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

            if (!this->batch.has_value() || !this->texture.has_value())
            {
                return;
            }

            const auto seconds = static_cast<float>(this->elapsed_time);

            this->batch->begin(this->context().viewport.projection());

            // Anchored at the top-left corner of virtual space, at its natural
            // orientation. The red marker texel belongs in *its* top-left corner,
            // which is the whole scene's answer to which way up everything is.
            this->batch->draw(*this->texture, render::Sprite{
                                                  .position = {80.0f, 80.0f},
                                                  .size = {240.0f, 240.0f},
                                              });

            // Turning about its own middle. Rotation is what the affine transform
            // was chosen for, and a sprite that stays put while it turns is what
            // proves the origin is carried through the same axes as the corners.
            this->batch->draw(*this->texture, render::Sprite{
                                                  .position = {960.0f, 540.0f},
                                                  .size = {420.0f, 420.0f},
                                                  .origin = {0.5f, 0.5f},
                                                  .rotation = seconds * 0.6f,
                                              });

            // Overlapping the one above with a pulsing alpha, so the blend mode the
            // batch enables is visible rather than merely configured.
            const float pulse = 0.35f + 0.25f * std::sin(seconds * 2.0f);
            this->batch->draw(*this->texture, render::Sprite{
                                                  .position = {1120.0f, 700.0f},
                                                  .size = {360.0f, 300.0f},
                                                  .origin = {0.5f, 0.5f},
                                                  .color = {0.4f, 1.0f, 0.6f, pulse},
                                              });

            // One texel of the texture, blown up. The region is in texels, so the
            // marker is asked for by the coordinates it was written at.
            this->batch->draw(*this->texture,
                              render::Sprite{
                                  .position = {1700.0f, 160.0f},
                                  .size = {160.0f, 160.0f},
                                  .origin = {0.5f, 0.5f},
                                  .region = {.position = {0.0f, 0.0f}, .size = {1.0f, 1.0f}},
                              });

            this->batch->end();
        }

    private:
        void create_scene()
        {
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
                // Reported, not fatal: the window stays up and the diagnostic
                // stays readable, which is the whole point of routing this through
                // std::expected instead of aborting.
                log::error(log::Category::RENDER, "{}", created_texture.error());
                return;
            }

            this->texture = std::move(*created_texture);

            auto created_batch = render::SpriteBatch::create();
            if (!created_batch)
            {
                log::error(log::Category::RENDER, "{}", created_batch.error());
                return;
            }

            this->batch = std::move(*created_batch);

            log::info(log::Category::RENDER, "scene ready: texture {}, batch of {}",
                      this->texture->id(), this->batch->capacity());
        }

        std::optional<render::Texture> texture;

        // Declared after the texture, and so destroyed before it. Nothing here
        // requires that order today — the batch holds no reference to the texture
        // between frames — but every other owner of GL objects in this codebase
        // arranges its fields so that the consumer dies first, and an exception
        // would have to be explained.
        std::optional<render::SpriteBatch> batch;

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
