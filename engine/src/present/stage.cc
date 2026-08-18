#include "cpen/present/stage.hh"

#include "cpen/core/log.hh"
#include "cpen/core/utf8.hh"

#include <algorithm>
#include <array>
#include <utility>

namespace cpen::present
{
    namespace
    {
        /// The vertical every anchor places a sprite at: the bottom of the
        /// reference screen. See position_of() for why there is only one.
        constexpr double FLOOR_LINE = 1.0;

        /// One row per anchor, in the enumerator's own order, so the lookup is an
        /// index rather than a switch that a new anchor could be left out of.
        ///
        /// The two off-screen places are a fifth of a screen beyond the edge,
        /// which is enough to hide a sprite of any reasonable width and close
        /// enough that a movement towards the stage does not take a visible age
        /// once something animates it.
        constexpr std::array<double, 7> ANCHOR_HORIZONTALS = {
            -0.2,   // OFFSCREEN_LEFT
            0.12,   // FAR_LEFT
            0.25,   // LEFT
            0.5,    // CENTER
            0.75,   // RIGHT
            0.88,   // FAR_RIGHT
            1.2,    // OFFSCREEN_RIGHT
        };

        constexpr std::size_t anchor_index(const Anchor anchor) noexcept
        {
            return static_cast<std::size_t>(anchor);
        }
    }

    std::string_view to_string(const Anchor anchor) noexcept
    {
        switch (anchor)
        {
            case Anchor::OFFSCREEN_LEFT:  return "offscreen_left";
            case Anchor::FAR_LEFT:        return "far_left";
            case Anchor::LEFT:            return "left";
            case Anchor::CENTER:          return "center";
            case Anchor::RIGHT:           return "right";
            case Anchor::FAR_RIGHT:       return "far_right";
            case Anchor::OFFSCREEN_RIGHT: return "offscreen_right";
        }

        return "unknown";
    }

    std::optional<Anchor> anchor_from_name(const std::string_view name) noexcept
    {
        // "centre" is not in to_string(), so it never comes back out of the
        // engine; it is accepted going in because it is the same word.
        if (name == "centre")
        {
            return Anchor::CENTER;
        }

        constexpr std::array<Anchor, 7> EVERY_ANCHOR = {
            Anchor::OFFSCREEN_LEFT, Anchor::FAR_LEFT, Anchor::LEFT, Anchor::CENTER,
            Anchor::RIGHT, Anchor::FAR_RIGHT, Anchor::OFFSCREEN_RIGHT,
        };

        for (const Anchor anchor : EVERY_ANCHOR)
        {
            if (to_string(anchor) == name)
            {
                return anchor;
            }
        }

        return std::nullopt;
    }

    script::ScreenPosition position_of(const Anchor anchor) noexcept
    {
        return script::ScreenPosition{
            .x = ANCHOR_HORIZONTALS[anchor_index(anchor)],
            .y = FLOOR_LINE,
        };
    }

    std::string_view layer_of(const std::string_view asset) noexcept
    {
        const std::size_t separator = asset.find('/');

        return separator == std::string_view::npos ? asset : asset.substr(0, separator);
    }

    script::ScreenPosition placement_of(const StageSprite& sprite) noexcept
    {
        if (sprite.position.has_value())
        {
            return *sprite.position;
        }

        // An unknown anchor was already reported where it entered the stage;
        // reporting it again on every frame that draws the sprite would bury the
        // one useful line under thousands of copies.
        if (const std::optional<Anchor> named = anchor_from_name(sprite.anchor))
        {
            return position_of(*named);
        }

        return position_of(Anchor::CENTER);
    }

    void Stage::say(const script::SayCommand& command)
    {
        this->current_line = command;
        this->begin_line();
    }

    void Stage::offer(const script::MenuCommand& command)
    {
        this->current_choices = command.prompts;
        this->menu_open = true;

        // A menu the reader has to answer while the question is still typing
        // itself out is a menu offered before it can be read. The choices are the
        // point at which the machine stops, so the line that introduced them is
        // finished here whatever the typewriter had left.
        this->complete_reveal();
    }

    void Stage::scene(const script::SceneCommand& command)
    {
        this->current_background = command.background;
        this->current_background_transition = command.transition;
    }

    void Stage::show(const script::ShowCommand& command)
    {
        if (!command.anchor.empty() && !anchor_from_name(command.anchor).has_value())
        {
            log::warn(log::Category::PRESENT,
                         "'{}' is not a place this engine knows; '{}' is shown at the "
                         "centre instead",
                         command.anchor, command.asset);
        }

        const std::string_view layer = layer_of(command.asset);

        StageSprite entry{
            .layer = std::string{layer},
            .asset = command.asset,
            .anchor = command.anchor,
            .position = command.position,
            .transition = command.transition,
        };

        const auto existing = std::ranges::find(this->stage_sprites, layer, &StageSprite::layer);

        // Assigned in place rather than erased and appended: the position in this
        // vector is the drawing order, and a character who changes expression
        // must not move in front of the one standing beside her.
        if (existing != this->stage_sprites.end())
        {
            *existing = std::move(entry);
            return;
        }

        this->stage_sprites.push_back(std::move(entry));
    }

    void Stage::hide(const script::HideCommand& command)
    {
        const auto removed = std::ranges::remove(this->stage_sprites, command.name,
                                                 &StageSprite::layer);

        if (removed.empty())
        {
            // Not a fault: the story ran, nothing is broken, and the author asked
            // for a state the stage is already in. Worth saying once, because the
            // usual cause is a misspelt name that will keep the sprite on screen
            // for the rest of the scene.
            log::warn(log::Category::PRESENT,
                         "nothing called '{}' is on the stage to hide", command.name);
            return;
        }

        this->stage_sprites.erase(removed.begin(), removed.end());
    }

    void Stage::begin_line()
    {
        this->boundaries.clear();
        this->revealed = 0.0;

        if (!this->current_line.has_value())
        {
            return;
        }

        const std::string_view text = this->current_line->text;

        this->boundaries.push_back(0);

        for (std::size_t offset = 0; offset < text.size();)
        {
            const core::DecodedCodePoint decoded = core::decode_utf8(text, offset);

            // decode_utf8 reports a zero size for input it cannot advance over;
            // stepping one byte keeps a malformed line finite rather than typing
            // it out forever.
            offset += decoded.size == 0 ? 1 : decoded.size;
            this->boundaries.push_back(std::min(offset, text.size()));
        }

        if (this->speed <= 0.0)
        {
            this->complete_reveal();
        }
    }

    void Stage::advance_reveal(const double delta_time) noexcept
    {
        if (!this->current_line.has_value() || this->speed <= 0.0 || delta_time <= 0.0)
        {
            return;
        }

        this->revealed += this->speed * delta_time;

        const double total = static_cast<double>(this->boundaries.size() - 1);

        this->revealed = std::min(this->revealed, total);
    }

    void Stage::complete_reveal() noexcept
    {
        if (this->boundaries.empty())
        {
            this->revealed = 0.0;
            return;
        }

        this->revealed = static_cast<double>(this->boundaries.size() - 1);
    }

    bool Stage::reveal_complete() const noexcept
    {
        if (!this->current_line.has_value() || this->boundaries.empty())
        {
            return true;
        }

        return this->revealed >= static_cast<double>(this->boundaries.size() - 1);
    }

    std::string_view Stage::revealed_text() const noexcept
    {
        if (!this->current_line.has_value() || this->boundaries.empty())
        {
            return {};
        }

        const auto whole = static_cast<std::size_t>(this->revealed);
        const std::size_t index = std::min(whole, this->boundaries.size() - 1);

        return std::string_view{this->current_line->text}.substr(0, this->boundaries[index]);
    }

    const StageSprite* Stage::sprite(const std::string_view layer) const noexcept
    {
        const auto found = std::ranges::find(this->stage_sprites, layer, &StageSprite::layer);

        return found == this->stage_sprites.end() ? nullptr : &*found;
    }

    void Stage::close_menu() noexcept
    {
        this->menu_open = false;
        this->current_choices.clear();
    }

    void Stage::clear() noexcept
    {
        this->current_background.clear();
        this->current_background_transition.clear();
        this->stage_sprites.clear();
        this->current_line.reset();
        this->boundaries.clear();
        this->revealed = 0.0;
        this->close_menu();
    }
}
