#include <cpen/app/application.hh>
#include <cpen/core/log.hh>
#include <cpen/render/draw.hh>
#include <cpen/render/font.hh>
#include <cpen/render/image.hh>
#include <cpen/render/pixel_format.hh>
#include <cpen/render/sprite.hh>
#include <cpen/render/sprite_batch.hh>
#include <cpen/render/text.hh>
#include <cpen/render/texture.hh>
#include <cpen/render/viewport.hh>
#include <cpen/runtime/game_context.hh>
#include <cpen/runtime/game_state.hh>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

    constexpr std::uint32_t TITLE_PIXEL_SIZE = 64;
    constexpr std::uint32_t BODY_PIXEL_SIZE = 30;

    /// Width the paragraph is wrapped to, in virtual pixels.
    constexpr float PARAGRAPH_WIDTH = 760.0f;

    constexpr std::string_view DEMO_TITLE = "C-Pen";

    constexpr std::string_view DEMO_PARAGRAPH =
        "Текст рисуется теми же спрайтами, что и картинки: глиф — это область "
        "атласа, а строка — один вызов отрисовки.\n"
        "Перенос считает кодовые точки, а не байты.";

    /// Finds a typeface installed on the machine.
    ///
    /// TODO(F2): the engine has to ship a typeface of its own — a game cannot
    /// assume the player has one — but that belongs with the asset layer, together
    /// with everything else that needs a path to come from somewhere honest.
    /// Borrowing one keeps the repository free of binary fixtures until then, at
    /// the price of a demo that says so and carries on without text if there is
    /// nothing to borrow.
    std::optional<std::filesystem::path> find_system_font()
    {
        constexpr std::array<const char*, 6> CANDIDATES = {
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/arial.ttf",
        };

        for (const char* const candidate : CANDIDATES)
        {
            std::error_code error;
            if (std::filesystem::exists(candidate, error))
            {
                return std::filesystem::path{candidate};
            }
        }

        return std::nullopt;
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
            this->body_font.reset();
            this->title_font.reset();
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

            this->draw_text_block();

            this->batch->end();

            if (!this->reported_batch)
            {
                this->reported_batch = true;
                log::info(log::Category::RENDER,
                          "first frame: {} sprite(s) in {} draw call(s)",
                          this->batch->sprite_count(), this->batch->draw_calls());
            }
        }

    private:
        /// The title and a wrapped paragraph, in the lower-left of the scene.
        ///
        /// Submitted into the same open batch as the sprites above, which is the
        /// claim worth seeing on screen: text is not a second renderer with a
        /// second shader, it is sprites against a different texture.
        void draw_text_block()
        {
            if (!this->title_font.has_value() || !this->body_font.has_value())
            {
                return;
            }

            constexpr glm::vec2 TEXT_ORIGIN{80.0f, 600.0f};
            constexpr glm::vec4 TITLE_COLOR{0.95f, 0.85f, 0.55f, 1.0f};
            constexpr glm::vec4 BODY_COLOR{0.85f, 0.88f, 0.95f, 1.0f};

            render::draw_text(*this->batch, *this->title_font, DEMO_TITLE, TEXT_ORIGIN,
                              TITLE_COLOR);

            // Measured rather than guessed, so the paragraph sits under the title
            // whatever the borrowed typeface turns out to be.
            const glm::vec2 title_box = render::measure_text(*this->title_font, DEMO_TITLE);

            glm::vec2 pen{TEXT_ORIGIN.x, TEXT_ORIGIN.y + title_box.y};

            for (const std::string_view line :
                 render::wrap_text(*this->body_font, DEMO_PARAGRAPH, PARAGRAPH_WIDTH))
            {
                render::draw_text(*this->batch, *this->body_font, line, pen, BODY_COLOR);
                pen.y += this->body_font->line_height();
            }
        }

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

            this->load_fonts();

            log::info(log::Category::RENDER, "scene ready: texture {}, batch of {}",
                      this->texture->id(), this->batch->capacity());
        }

        void load_fonts()
        {
            const std::optional<std::filesystem::path> path = find_system_font();
            if (!path.has_value())
            {
                log::warn(log::Category::RENDER,
                          "no typeface was found to borrow; the demo runs without text");
                return;
            }

            auto title = render::Font::from_file(*path, TITLE_PIXEL_SIZE);
            auto body = render::Font::from_file(*path, BODY_PIXEL_SIZE);

            if (!title || !body)
            {
                log::error(log::Category::RENDER, "{}",
                           title ? body.error() : title.error());
                return;
            }

            this->title_font = std::move(*title);
            this->body_font = std::move(*body);
        }

        std::optional<render::Texture> texture;

        // Two sizes of one typeface, which the engine treats exactly as two
        // different typefaces would be treated: each owns its atlas, and drawing
        // with both splits the batch the same way two pictures would. Held as
        // separate objects rather than selected by a parameter, so that changing
        // the font at runtime is a matter of passing a different one.
        std::optional<render::Font> title_font;
        std::optional<render::Font> body_font;

        // Declared after the texture, and so destroyed before it. Nothing here
        // requires that order today — the batch holds no reference to the texture
        // between frames — but every other owner of GL objects in this codebase
        // arranges its fields so that the consumer dies first, and an exception
        // would have to be explained.
        std::optional<render::SpriteBatch> batch;

        double elapsed_time = 0.0;

        /// Reports the batch's tally once rather than sixty times a second.
        bool reported_batch = false;
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
