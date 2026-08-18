#include <cpen/app/application.hh>
#include <cpen/app/asset_roots.hh>
#include <cpen/assets/asset_manager.hh>
#include <cpen/core/error.hh>
#include <cpen/core/log.hh>
#include <cpen/render/font.hh>
#include <cpen/render/image.hh>
#include <cpen/render/pixel_format.hh>
#include <cpen/render/renderer.hh>
#include <cpen/render/sprite.hh>
#include <cpen/render/sprite_batch.hh>
#include <cpen/render/text.hh>
#include <cpen/render/texture.hh>
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
#include <utility>
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

    /// Builds a texture from generated pixels the way the asset layer builds one
    /// from a file.
    ///
    /// Premultiplied, because that is the space the sprite batch blends in.
    /// Anything reaching it straight is composited twice against its own alpha and
    /// comes out too dark wherever it is not opaque.
    std::expected<render::Texture, core::Error> make_texture(
        std::vector<std::byte> pixels, const std::uint32_t width, const std::uint32_t height,
        const render::TextureConfig& config = {})
    {
        return render::Image::from_pixels(std::move(pixels), width, height,
                                          render::PixelFormat::RGBA8)
            .and_then([&config](render::Image image) -> std::expected<render::Texture, core::Error>
            {
                image.premultiply_alpha();
                return render::Texture::from_image(image, config);
            });
    }

    constexpr std::uint32_t TITLE_PIXEL_SIZE = 64;
    constexpr std::uint32_t BODY_PIXEL_SIZE = 30;

    /// Width the paragraph is wrapped to, in virtual pixels.
    constexpr float PARAGRAPH_WIDTH = 760.0f;

    constexpr std::string_view DEMO_TITLE = "C-Pen";

    constexpr std::string_view DEMO_PARAGRAPH =
        "Текст рисуется теми же спрайтами, что и картинки: глиф — это область "
        "атласа, а строка — один вызов отрисовки.\n"
        "Слева вверху текстура растянута в полсотни раз без фильтрации, отчего "
        "видна и лесенка на границах клеток, и ступенчатый край квада. В центре — "
        "то же самое так, как устроен настоящий спрайт.";

    constexpr std::string_view CRISP_LABEL = "8x8, nearest, край непрозрачный";
    constexpr std::string_view SMOOTH_LABEL = "128x128, linear, кайма прозрачная";

    /// The identifier of the demo's one loaded picture.
    ///
    /// A logical name, not a path: the engine turns it into
    /// "assets/sprites/checker_soft.png" under the game's root by convention, and
    /// nothing here knows or can know where that root is.
    ///
    /// The file is the same pattern as the generated checkerboard, drawn the way a
    /// real asset is: large enough to filter, and with a fully transparent border
    /// so the silhouette comes from the alpha channel rather than from the edge of
    /// the quad. Its transparent texels carry black, exactly as an image editor
    /// exports them — which is what used to produce a dark halo, and what the
    /// premultiplication on load now removes.
    constexpr std::string_view SOFT_SPRITE = "checker_soft";

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

            // The background belongs to the frame rather than to this state, so it
            // is set once here instead of being cleared to on every render().
            this->context().renderer.set_clear_color(BACKGROUND_COLOR);

            this->create_scene();
        }

        /// GL resources are released here rather than in the destructor because
        /// the stack calls on_exit() while the context is still current:
        /// Application declares the stack after the window, so the stack is torn
        /// down first and the context outlives every state.
        void on_exit() override
        {
            this->body_font.reset();
            this->title_font.reset();

            this->soft_drawable = nullptr;
            this->soft_texture.reset();

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
                           this->context().renderer.viewport().rect().width,
                           this->context().renderer.viewport().rect().height,
                           this->context().renderer.viewport().rect().x,
                           this->context().renderer.viewport().rect().y);
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
            // Neither the clear nor the batch is this state's to open: the
            // Application does both, once, either side of the whole render pass.
            // What arrives here is a batch already open on the frame's projection,
            // shared with every other state in the stack.
            render::SpriteBatch* const batch = this->context().renderer.sprites();

            if (batch == nullptr || !this->texture.has_value())
            {
                return;
            }

            const auto seconds = static_cast<float>(this->elapsed_time);

            // Turning about its own middle. Rotation is what the affine transform
            // was chosen for, and a sprite that stays put while it turns is what
            // proves the origin is carried through the same axes as the corners.
            //
            // Drawn first, and from the other texture, so that the two textures
            // make two runs rather than three: the batch never reorders what it is
            // given, so grouping is the caller's to arrange.
            if (this->soft_drawable != nullptr)
            {
                batch->draw(*this->soft_drawable, render::Sprite{
                                                           .position = {960.0f, 540.0f},
                                                           .size = {420.0f, 420.0f},
                                                           .origin = {0.5f, 0.5f},
                                                           .rotation = seconds * 0.6f,
                                                       });
            }

            // Anchored at the top-left corner of virtual space, at its natural
            // orientation. The red marker texel belongs in *its* top-left corner,
            // which is the whole scene's answer to which way up everything is.
            //
            // Eight texels stretched across two hundred and forty pixels with no
            // filtering: the comparison against the one above, and the reason both
            // kinds of jagged edge are visible here and neither is there.
            batch->draw(*this->texture, render::Sprite{
                                                  .position = {80.0f, 80.0f},
                                                  .size = {240.0f, 240.0f},
                                              });

            // Overlapping the rotating sprite with a pulsing alpha, so the blend mode the
            // batch enables is visible rather than merely configured.
            const float pulse = 0.35f + 0.25f * std::sin(seconds * 2.0f);
            batch->draw(*this->texture, render::Sprite{
                                                  .position = {1120.0f, 700.0f},
                                                  .size = {360.0f, 300.0f},
                                                  .origin = {0.5f, 0.5f},
                                                  .color = {0.4f, 1.0f, 0.6f, pulse},
                                              });

            // One texel of the texture, blown up. The region is in texels, so the
            // marker is asked for by the coordinates it was written at.
            batch->draw(*this->texture,
                              render::Sprite{
                                  .position = {1700.0f, 160.0f},
                                  .size = {160.0f, 160.0f},
                                  .origin = {0.5f, 0.5f},
                                  .region = {.position = {0.0f, 0.0f}, .size = {1.0f, 1.0f}},
                              });

            this->draw_text_block(*batch);

            if (!this->reported_batch)
            {
                this->reported_batch = true;
                log::info(log::Category::RENDER, "first frame: {} sprite(s) submitted",
                          batch->sprite_count());
            }
        }

    private:
        /// The title and a wrapped paragraph, in the lower-left of the scene.
        ///
        /// Submitted into the same open batch as the sprites above, which is the
        /// claim worth seeing on screen: text is not a second renderer with a
        /// second shader, it is sprites against a different texture.
        void draw_text_block(render::SpriteBatch& batch)
        {
            if (!this->title_font || !this->body_font)
            {
                return;
            }

            constexpr glm::vec2 TEXT_ORIGIN{80.0f, 600.0f};
            constexpr glm::vec4 TITLE_COLOR{0.95f, 0.85f, 0.55f, 1.0f};
            constexpr glm::vec4 BODY_COLOR{0.85f, 0.88f, 0.95f, 1.0f};

            render::draw_text(batch, *this->title_font, DEMO_TITLE, TEXT_ORIGIN,
                              TITLE_COLOR);

            // Measured rather than guessed, so the paragraph sits under the title
            // whatever the borrowed typeface turns out to be.
            const glm::vec2 title_box = render::measure_text(*this->title_font, DEMO_TITLE);

            glm::vec2 pen{TEXT_ORIGIN.x, TEXT_ORIGIN.y + title_box.y};

            for (const std::string_view line :
                 render::wrap_text(*this->body_font, DEMO_PARAGRAPH, PARAGRAPH_WIDTH))
            {
                render::draw_text(batch, *this->body_font, line, pen, BODY_COLOR);
                pen.y += this->body_font->line_height();
            }

            // Both labels come after the paragraph and in the same font, so all the
            // body text is one run whatever order the sprites above were drawn in.
            constexpr glm::vec4 LABEL_COLOR{0.65f, 0.70f, 0.80f, 1.0f};

            this->draw_centred(batch, *this->body_font, CRISP_LABEL, glm::vec2{200.0f, 360.0f},
                               LABEL_COLOR);

            if (this->soft_drawable != nullptr)
            {
                this->draw_centred(batch, *this->body_font, SMOOTH_LABEL, glm::vec2{960.0f, 800.0f},
                                   LABEL_COLOR);
            }
        }

        /// Draws one line with its box centred on `centre`, which is what
        /// measure_text is for.
        void draw_centred(render::SpriteBatch& batch, render::Font& font,
                          const std::string_view text, const glm::vec2& centre,
                          const glm::vec4& color)
        {
            const glm::vec2 box = render::measure_text(font, text);
            render::draw_text(batch, font, text, centre - box * 0.5f, color);
        }

        void create_scene()
        {
            // Nearest filtering, because the checkerboard is eight texels across
            // and stretched over most of the window: linear filtering would blur
            // every cell boundary into a gradient and hide exactly what the
            // texture is here to show.
            auto created_texture = make_texture(
                generate_checkerboard(), CHECKERBOARD_SIZE, CHECKERBOARD_SIZE,
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

            // The other picture is not made here at all: it is a file in the
            // game's directory, asked for by name. Everything between the name and
            // the texture — finding the root, resolving the extension, reading the
            // bytes, decoding them, premultiplying the alpha, uploading — belongs
            // to the asset layer, and none of it is visible from here.
            assets::AssetManager& asset_manager = this->context().assets;

            if (auto loaded = asset_manager.texture(assets::AssetKind::SPRITE, SOFT_SPRITE))
            {
                this->soft_texture = std::move(*loaded);
                this->soft_drawable = this->soft_texture.get();
            }
            else
            {
                // What the presentation layer will do for every missing asset, in
                // miniature: the manager reports the failure and offers something
                // to draw, and the decision to draw it belongs here, where it is
                // known that there is a screen and something has to be on it.
                this->soft_drawable = asset_manager.placeholder_texture();
            }

            this->load_fonts();

            log::info(log::Category::RENDER, "scene ready: texture {}, drawing {}",
                      this->texture->id(),
                      this->context().renderer.can_draw() ? "enabled" : "unavailable");
        }

        /// Two sizes of the typeface the engine ships.
        ///
        /// No path, no fallback search, and no case where the demo runs without
        /// text: a game that ships no typeface of its own still has this one,
        /// because the engine's root is mounted underneath the game's. A game that
        /// wants its own puts a file at "assets/fonts/default.ttf" and this code
        /// does not change.
        void load_fonts()
        {
            assets::AssetManager& asset_manager = this->context().assets;

            auto title = asset_manager.default_font(TITLE_PIXEL_SIZE);
            auto body = asset_manager.default_font(BODY_PIXEL_SIZE);

            if (!title || !body)
            {
                log::error(log::Category::RENDER, "{}",
                           title ? body.error() : title.error());
                return;
            }

            this->title_font = std::move(*title);
            this->body_font = std::move(*body);
        }

        /// The one texture this state still builds itself, because it needs
        /// nearest-neighbour filtering and the asset layer has nowhere to be told
        /// that yet: how a picture is filtered is a property of the asset, and the
        /// manifest that will carry it does not exist. A texture that wants
        /// anything but the default is made by hand until it does.
        std::optional<render::Texture> texture;

        /// A reference to an asset the manager owns, not the asset itself. Dropping
        /// it gives up a claim on the picture; it does not unload it, and it cannot
        /// outlive the manager, which the Application destroys before the window.
        assets::TextureReference soft_texture;

        /// What to draw for it: the loaded picture, or the manager's placeholder if
        /// there was none. Kept as a pointer because those two are owned by
        /// different things and only one of them is an asset.
        const render::Texture* soft_drawable = nullptr;

        // Two sizes of one typeface, which the engine treats exactly as two
        // different typefaces would be treated: each owns its atlas, and drawing
        // with both splits the batch the same way two pictures would. The size is
        // part of the manager's key for the same reason.
        assets::FontReference title_font;
        assets::FontReference body_font;

        // Declared after the texture, and so destroyed before it. Nothing here
        // requires that order today — the batch holds no reference to the texture
        // between frames — but every other owner of GL objects in this codebase
        // arranges its fields so that the consumer dies first, and an exception
        // would have to be explained.
        double elapsed_time = 0.0;

        /// Reports the batch's tally once rather than sixty times a second.
        bool reported_batch = false;
    };
}

/// Entry point for the C-Pen demo game executable.
///
/// The command line is read for one reason: to say where the game's files are.
/// Without it they are the ones beside this executable, which is what a player
/// gets; with `--game <path>` the same binary runs a different game directory,
/// which is what an author editing one wants.
int main(const int argument_count, const char* const* const arguments)
{
    log::initialize_console();
    log::info(log::Category::APP, "C-Pen demo starting");

    auto roots = app::asset_roots_from_command_line(argument_count, arguments);

    if (!roots)
    {
        // Fatal, unlike almost everything else here. Every other failure leaves a
        // window up with a diagnostic in it; this one means the player asked for a
        // directory and did not say which, and starting on the wrong one would
        // report every asset in the game as missing.
        log::error(log::Category::APP, "{}", roots.error());
        return 1;
    }

    app::Application::Config configuration;
    configuration.roots = std::move(*roots);

    app::Application application{std::move(configuration)};
    application.states().push(std::make_unique<DemoState>());
    application.run();

    log::info(log::Category::APP, "C-Pen demo shutting down");
    return 0;
}
