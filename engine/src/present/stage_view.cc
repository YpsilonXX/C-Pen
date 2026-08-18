#include "cpen/present/stage_view.hh"

#include "cpen/core/log.hh"
#include "cpen/render/font.hh"
#include "cpen/render/pixel_format.hh"
#include "cpen/render/sprite.hh"
#include "cpen/render/sprite_batch.hh"
#include "cpen/render/text.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace cpen::present
{
    namespace
    {
        /// The key a picture is cached under. Backgrounds and sprites are
        /// different kinds and could name the same identifier, so the kind is part
        /// of the key rather than a detail of how it is loaded.
        std::string picture_key(const assets::AssetKind kind, const std::string& identifier)
        {
            return std::string{assets::to_string(kind)} + ':' + identifier;
        }

        /// Turns a rectangle into the sprite that fills it. Nothing here rotates,
        /// so the origin stays at the corner and the position is the corner.
        render::Sprite quad(const Rectangle& area, const glm::vec4& color)
        {
            return render::Sprite{
                .position = area.position,
                .size = area.size,
                .color = color,
            };
        }
    }

    StageView::StageView(assets::AssetManager& asset_manager, DialogueTheme theme_value)
        : assets(&asset_manager), dialogue_theme(std::move(theme_value))
    {
    }

    void StageView::load()
    {
        auto body = this->assets->font(this->dialogue_theme.font_identifier,
                                       this->dialogue_theme.body_pixel_size);
        auto speaker = this->assets->font(this->dialogue_theme.font_identifier,
                                          this->dialogue_theme.speaker_pixel_size);

        if (body.has_value())
        {
            this->body_font = std::move(*body);
        }
        else
        {
            log::error(log::Category::PRESENT, "the dialogue has no typeface: {}", body.error());
        }

        if (speaker.has_value())
        {
            this->speaker_font = std::move(*speaker);
        }

        // Straight white, and opaque: premultiplying an opaque colour changes
        // nothing, so this texel is already in the space the batch blends in.
        constexpr std::array<std::byte, 4> WHITE_TEXEL = {
            std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        };

        auto created = render::Texture::from_pixels(WHITE_TEXEL, 1, 1,
                                                    render::PixelFormat::RGBA8);

        if (created.has_value())
        {
            this->panel_texture = std::move(*created);
        }
        else
        {
            log::error(log::Category::PRESENT, "the dialogue has no panel texture: {}",
                       created.error());
        }
    }

    void StageView::release()
    {
        this->pictures.clear();
        this->panel_texture.reset();
        this->speaker_font.reset();
        this->body_font.reset();
    }

    const render::Texture* StageView::picture(const assets::AssetKind kind,
                                              const std::string& identifier)
    {
        if (identifier.empty())
        {
            return nullptr;
        }

        const std::string key = picture_key(kind, identifier);

        if (const auto found = this->pictures.find(key); found != this->pictures.end())
        {
            return found->second.failed ? this->assets->placeholder_texture()
                                        : found->second.reference.get();
        }

        auto loaded = this->assets->texture(kind, identifier);

        Picture entry;

        if (loaded.has_value())
        {
            entry.reference = std::move(*loaded);
        }
        else
        {
            // Reported by the manager already, and recorded in its ledger. What is
            // decided here is what to do about it, which is the presentation
            // layer's business: something has to be on screen, so the placeholder
            // goes there and the story carries on.
            entry.failed = true;
        }

        const Picture& stored = this->pictures.emplace(key, std::move(entry)).first->second;

        return stored.failed ? this->assets->placeholder_texture() : stored.reference.get();
    }

    void StageView::forget_unused(const Stage& stage)
    {
        if (this->pictures.empty())
        {
            return;
        }

        // Small enough to sweep: a stage holds a background and a handful of
        // sprites, so this is a few string comparisons per frame and it keeps a
        // long game from accumulating every picture it has ever shown.
        std::erase_if(this->pictures, [&stage](const auto& entry)
        {
            if (entry.first == picture_key(assets::AssetKind::BACKGROUND, stage.background()))
            {
                return false;
            }

            for (const StageSprite& sprite : stage.sprites())
            {
                if (entry.first == picture_key(assets::AssetKind::SPRITE, sprite.asset))
                {
                    return false;
                }
            }

            return true;
        });
    }

    void StageView::draw_panel(render::SpriteBatch& batch, const Rectangle& area,
                               const glm::vec4& color)
    {
        if (!this->panel_texture.has_value() || area.size.x <= 0.0f || area.size.y <= 0.0f)
        {
            return;
        }

        batch.draw(*this->panel_texture, quad(area, color));
    }

    void StageView::draw_background(render::SpriteBatch& batch, const Stage& stage,
                                    const glm::vec2& screen)
    {
        if (stage.background().empty())
        {
            return;
        }

        const render::Texture* const texture =
            this->picture(assets::AssetKind::BACKGROUND, stage.background());

        if (texture == nullptr)
        {
            return;
        }

        // Stretched over the reference screen rather than drawn at its own size:
        // a background is the screen, and one exported a few pixels short would
        // otherwise leave a seam down the side.
        batch.draw(*texture, quad(background_rectangle(screen), glm::vec4{1.0f}));
    }

    void StageView::draw_sprites(render::SpriteBatch& batch, const Stage& stage,
                                 const glm::vec2& screen)
    {
        for (const StageSprite& sprite : stage.sprites())
        {
            const render::Texture* const texture =
                this->picture(assets::AssetKind::SPRITE, sprite.asset);

            if (texture == nullptr)
            {
                continue;
            }

            const glm::vec2 size{static_cast<float>(texture->width()),
                                 static_cast<float>(texture->height())};

            batch.draw(*texture,
                       quad(sprite_rectangle(placement_of(sprite), size, screen),
                            glm::vec4{1.0f}));
        }
    }

    void StageView::draw_textbox(render::SpriteBatch& batch, const Stage& stage,
                                 const glm::vec2& screen)
    {
        if (!stage.line().has_value())
        {
            return;
        }

        this->draw_panel(batch, textbox_rectangle(this->dialogue_theme, screen),
                         this->dialogue_theme.box_color);

        if (!this->has_text())
        {
            return;
        }

        float speaker_height = 0.0f;

        if (const std::optional<std::string>& speaker = stage.line()->speaker)
        {
            const glm::vec2 origin = speaker_origin(this->dialogue_theme, screen);

            render::draw_text(batch, *this->speaker_font, *speaker, origin,
                              this->dialogue_theme.speaker_color);

            speaker_height = this->speaker_font->line_height();
        }

        const std::string_view whole = stage.line()->text;

        // Wrapped from the whole line rather than from the part revealed so far.
        // Wrapping a growing prefix would reflow the paragraph on almost every
        // frame, and words already read would jump between lines as the next one
        // arrived.
        const std::vector<std::string_view> lines =
            render::wrap_text(*this->body_font, whole, line_width(this->dialogue_theme, screen));

        const std::size_t revealed = stage.revealed_text().size();

        glm::vec2 pen = line_origin(this->dialogue_theme, screen, speaker_height);

        for (const std::string_view line : lines)
        {
            // The wrapped pieces are views into the line itself, so where a piece
            // begins is arithmetic on pointers rather than a second count of the
            // text, and it agrees exactly with what the typewriter measured.
            const auto begin = static_cast<std::size_t>(line.data() - whole.data());

            if (begin >= revealed)
            {
                break;
            }

            const std::size_t visible = std::min(line.size(), revealed - begin);

            render::draw_text(batch, *this->body_font, line.substr(0, visible), pen,
                              this->dialogue_theme.text_color);

            pen.y += this->body_font->line_height();
        }
    }

    void StageView::draw_menu(render::SpriteBatch& batch, const Stage& stage,
                              const glm::vec2& screen,
                              const std::optional<std::size_t> highlighted)
    {
        if (!stage.has_menu())
        {
            return;
        }

        const std::vector<Rectangle> areas =
            choice_rectangles(this->dialogue_theme, screen, stage.choices().size());

        for (std::size_t index = 0; index < areas.size(); ++index)
        {
            const bool lit = highlighted.has_value() && *highlighted == index;

            this->draw_panel(batch, areas[index],
                             lit ? this->dialogue_theme.choice_highlight_color
                                 : this->dialogue_theme.choice_color);

            if (!this->has_text())
            {
                continue;
            }

            const std::string& prompt = stage.choices()[index];
            const glm::vec2 box = render::measure_text(*this->body_font, prompt);

            // Centred in its panel on both axes, measured rather than guessed, so
            // a prompt of any length sits right in a panel of a fixed size.
            const glm::vec2 origin = areas[index].position + (areas[index].size - box) * 0.5f;

            render::draw_text(batch, *this->body_font, prompt, origin,
                              this->dialogue_theme.choice_text_color);
        }
    }

    void StageView::draw_message(render::SpriteBatch& batch, const glm::vec2& screen,
                                 const std::string_view text)
    {
        // Opaque, and over everything: a diagnostic read against a background of
        // the scene that produced it is a diagnostic misread.
        constexpr glm::vec4 BACKDROP{0.08f, 0.03f, 0.05f, 1.0f};

        this->draw_panel(batch, background_rectangle(screen), BACKDROP);

        if (!this->has_text())
        {
            return;
        }

        const float padding = this->dialogue_theme.box_padding;

        glm::vec2 pen{padding, padding};

        for (const std::string_view line :
             render::wrap_text(*this->body_font, text, screen.x - 2.0f * padding))
        {
            render::draw_text(batch, *this->body_font, line, pen,
                              this->dialogue_theme.text_color);

            pen.y += this->body_font->line_height();

            if (pen.y > screen.y - padding)
            {
                // A compiler can produce more complaints than a screen has room
                // for. The rest are in the log, whole; what stops here is the copy
                // on screen, not the report.
                break;
            }
        }
    }

    void StageView::draw(render::SpriteBatch& batch, const Stage& stage, const glm::vec2& screen,
                         const std::optional<std::size_t> highlighted)
    {
        this->forget_unused(stage);

        // Back to front, and grouped by texture where that is free: the batch
        // never reorders what it is given, so the background, then the sprites,
        // then every panel of the interface is both the right order to look at
        // and close to the fewest draw calls it can be done in.
        this->draw_background(batch, stage, screen);
        this->draw_sprites(batch, stage, screen);
        this->draw_textbox(batch, stage, screen);
        this->draw_menu(batch, stage, screen, highlighted);
    }
}
