#include "cpen/present/layout.hh"

#include <algorithm>

namespace cpen::present
{
    Rectangle textbox_rectangle(const DialogueTheme& theme, const glm::vec2& screen) noexcept
    {
        const float width = std::max(screen.x - 2.0f * theme.box_margin, 0.0f);
        const float height = std::min(theme.box_height, screen.y);

        return Rectangle{
            .position = {theme.box_margin, screen.y - theme.box_margin - height},
            .size = {width, height},
        };
    }

    glm::vec2 speaker_origin(const DialogueTheme& theme, const glm::vec2& screen) noexcept
    {
        const Rectangle box = textbox_rectangle(theme, screen);

        return box.position + glm::vec2{theme.box_padding, theme.box_padding};
    }

    glm::vec2 line_origin(const DialogueTheme& theme, const glm::vec2& screen,
                          const float speaker_height) noexcept
    {
        glm::vec2 origin = speaker_origin(theme, screen);

        // Narration has no name above it and no gap under the name that is not
        // there: the line starts where the name would have.
        if (speaker_height > 0.0f)
        {
            origin.y += speaker_height + theme.speaker_gap;
        }

        return origin;
    }

    float line_width(const DialogueTheme& theme, const glm::vec2& screen) noexcept
    {
        const Rectangle box = textbox_rectangle(theme, screen);

        return std::max(box.size.x - 2.0f * theme.box_padding, 0.0f);
    }

    std::vector<Rectangle> choice_rectangles(const DialogueTheme& theme,
                                             const glm::vec2& screen,
                                             const std::size_t count)
    {
        std::vector<Rectangle> rectangles;

        if (count == 0)
        {
            return rectangles;
        }

        rectangles.reserve(count);

        const float step = theme.choice_height + theme.choice_spacing;

        // The spacing is between choices, so there is one fewer of it than there
        // are choices; adding it after the last would push the block up by half a
        // gap and make a two-choice menu look untidy against a three-choice one.
        const float total = static_cast<float>(count) * theme.choice_height +
                            static_cast<float>(count - 1) * theme.choice_spacing;

        // Centred in what is left above the text box rather than on the screen:
        // a menu whose lower half is behind the box is a menu with unreachable
        // answers in it.
        const float available = textbox_rectangle(theme, screen).position.y;
        const float top = std::max((available - total) * 0.5f, 0.0f);

        const float left = (screen.x - theme.choice_width) * 0.5f;

        for (std::size_t index = 0; index < count; ++index)
        {
            rectangles.push_back(Rectangle{
                .position = {left, top + static_cast<float>(index) * step},
                .size = {theme.choice_width, theme.choice_height},
            });
        }

        return rectangles;
    }

    std::optional<std::size_t> choice_at(const DialogueTheme& theme, const glm::vec2& screen,
                                         const std::size_t count, const glm::vec2& point)
    {
        const std::vector<Rectangle> rectangles = choice_rectangles(theme, screen, count);

        for (std::size_t index = 0; index < rectangles.size(); ++index)
        {
            if (rectangles[index].contains(point))
            {
                return index;
            }
        }

        return std::nullopt;
    }

    Rectangle sprite_rectangle(const script::ScreenPosition& placement,
                               const glm::vec2& sprite_size,
                               const glm::vec2& screen) noexcept
    {
        const glm::vec2 point{
            static_cast<float>(placement.x) * screen.x,
            static_cast<float>(placement.y) * screen.y,
        };

        return Rectangle{
            .position = {point.x - sprite_size.x * 0.5f, point.y - sprite_size.y},
            .size = sprite_size,
        };
    }

    Rectangle background_rectangle(const glm::vec2& screen) noexcept
    {
        return Rectangle{.position = {0.0f, 0.0f}, .size = screen};
    }
}
