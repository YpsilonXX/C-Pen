#ifndef CPEN_PRESENT_LAYOUT_HH
#define CPEN_PRESENT_LAYOUT_HH

#include "cpen/script/command_sink.hh"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cpen::present
{
    /// A rectangle in virtual pixels, measured from the top left of the reference
    /// screen — the same space sprites are positioned in and the same space a
    /// mouse position is converted into.
    struct Rectangle
    {
        glm::vec2 position{0.0f, 0.0f};
        glm::vec2 size{0.0f, 0.0f};

        bool contains(const glm::vec2& point) const noexcept
        {
            return point.x >= this->position.x &&
                   point.y >= this->position.y &&
                   point.x < this->position.x + this->size.x &&
                   point.y < this->position.y + this->size.y;
        }

        float right() const noexcept { return this->position.x + this->size.x; }
        float bottom() const noexcept { return this->position.y + this->size.y; }
    };

    /// Every measurement and colour the dialogue is drawn with.
    ///
    /// A value the game owns rather than constants in the drawing code: the look
    /// of a text box is the first thing a game changes, and changing it must not
    /// mean editing the engine. Every length is in virtual pixels against the
    /// reference screen, so a theme means the same thing at any window size.
    struct DialogueTheme
    {
        /// Distance from the text box to the left, right and bottom edges.
        float box_margin = 48.0f;

        float box_height = 280.0f;

        /// Distance from the inside of the box to the text in it.
        float box_padding = 40.0f;

        /// Gap between the speaker's name and the first line under it.
        float speaker_gap = 10.0f;

        std::uint32_t body_pixel_size = 34;
        std::uint32_t speaker_pixel_size = 38;

        glm::vec4 box_color{0.04f, 0.05f, 0.09f, 0.86f};
        glm::vec4 text_color{0.92f, 0.93f, 0.96f, 1.0f};
        glm::vec4 speaker_color{0.98f, 0.84f, 0.45f, 1.0f};

        float choice_width = 960.0f;
        float choice_height = 78.0f;
        float choice_spacing = 20.0f;

        glm::vec4 choice_color{0.10f, 0.12f, 0.20f, 0.90f};
        glm::vec4 choice_highlight_color{0.22f, 0.28f, 0.45f, 0.95f};
        glm::vec4 choice_text_color{0.94f, 0.95f, 0.98f, 1.0f};

        /// The typeface every part of the dialogue is set in. A logical name, the
        /// same one a script would write; the engine ships one under it, and a
        /// game that puts its own file there replaces it without changing code.
        std::string font_identifier{"default"};
    };

    /// The box the dialogue is written in: across the bottom, inset by the
    /// margin on three sides.
    Rectangle textbox_rectangle(const DialogueTheme& theme, const glm::vec2& screen) noexcept;

    /// Where the speaker's name is written, and where the line under it starts.
    ///
    /// Both are the top left of a text run rather than a box around it: the
    /// height a line needs is a property of the typeface, which layout does not
    /// have and does not need in order to say where the text begins.
    glm::vec2 speaker_origin(const DialogueTheme& theme, const glm::vec2& screen) noexcept;
    glm::vec2 line_origin(const DialogueTheme& theme, const glm::vec2& screen,
                          float speaker_height) noexcept;

    /// How wide a line may be before it has to wrap.
    float line_width(const DialogueTheme& theme, const glm::vec2& screen) noexcept;

    /// One rectangle per choice, in the order the script wrote them, stacked and
    /// centred in the space above the text box.
    std::vector<Rectangle> choice_rectangles(const DialogueTheme& theme,
                                             const glm::vec2& screen,
                                             std::size_t count);

    /// Which choice a point is over, if any.
    ///
    /// Computed from the same function the rectangles are drawn from rather than
    /// from a copy kept by whoever drew them: a hit test that disagrees with the
    /// picture is the classic way for a menu to answer the wrong question.
    std::optional<std::size_t> choice_at(const DialogueTheme& theme, const glm::vec2& screen,
                                         std::size_t count, const glm::vec2& point);

    /// Where a sprite of this size stands when placed at this point.
    ///
    /// The placement point is the middle of the sprite's bottom edge — see
    /// position_of() in stage.hh for why — and the sprite's size is its own, one
    /// texel to one virtual pixel, so how large a character is on screen is a
    /// property of the file the artist exported.
    Rectangle sprite_rectangle(const script::ScreenPosition& placement,
                               const glm::vec2& sprite_size,
                               const glm::vec2& screen) noexcept;

    /// The whole reference screen, which is where a background goes.
    Rectangle background_rectangle(const glm::vec2& screen) noexcept;
}

#endif //CPEN_PRESENT_LAYOUT_HH
