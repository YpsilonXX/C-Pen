#ifndef CPEN_RENDER_TEXT_HH
#define CPEN_RENDER_TEXT_HH

#include <glm/glm.hpp>

#include <functional>
#include <string_view>
#include <vector>

namespace cpen::render
{
    class Font;
    class SpriteBatch;

    /// How wide one code point is, in virtual pixels.
    ///
    /// The line breaker takes one of these instead of a Font so that the algorithm
    /// can be exercised without a typeface, a GL context or a driver — a test states
    /// that every character is ten wide and then reads the line breaks in round
    /// numbers. The Font overload below is the thin wrapper the engine actually
    /// calls.
    using AdvanceFunction = std::function<float(char32_t)>;

    /// Breaks `text` into lines no wider than `maximum_width`.
    ///
    /// Lines are returned as views into `text`, which must therefore outlive them.
    /// Nothing is copied and nothing is allocated per line beyond the vector itself.
    ///
    /// Breaks happen at spaces, and a run of spaces at a break is dropped rather
    /// than carried to the start of the next line. A single word too long to fit is
    /// broken where it reaches the width: refusing to break it would put a line
    /// past the edge of the box and no rewrapping would ever fix it. A newline in
    /// the text always breaks, whatever the width, and a width of zero or less
    /// breaks only at newlines.
    ///
    /// No hyphenation and no shaping: the engine places glyphs by their advances
    /// and nothing more. Correct for Latin and Cyrillic; not for scripts that need
    /// ligatures or reordering, which would need a shaping engine this does not
    /// have.
    std::vector<std::string_view> wrap_text(std::string_view text, float maximum_width,
                                            const AdvanceFunction& advance_of);

    /// Breaks `text` for a particular font.
    ///
    /// The font is taken by non-const reference because measuring a character may
    /// have to rasterise it first — see Font::glyph.
    std::vector<std::string_view> wrap_text(Font& font, std::string_view text,
                                            float maximum_width);

    /// The size of the box `text` occupies, in virtual pixels.
    ///
    /// The width is that of the widest line and the height is a whole number of
    /// line heights, so that two strings of different lengths still line up. A
    /// newline in the text starts a new line, and an empty string measures as one
    /// line high rather than as nothing — an empty label still takes up its row.
    glm::vec2 measure_text(Font& font, std::string_view text);

    /// Submits `text` to `batch` with the top-left of its first line box at
    /// `position`, in virtual pixels.
    ///
    /// The top-left of the *box*, not the baseline. A caller laying out a dialogue
    /// box thinks in boxes, and the baseline of the first line is recoverable as
    /// position.y + font.ascender() by anyone who needs it.
    ///
    /// Every glyph becomes an ordinary Sprite against the font's atlas, so a line
    /// of text is one draw call, text and pictures share one batch, and a change of
    /// font costs exactly what a change of texture costs — nothing more.
    void draw_text(SpriteBatch& batch, Font& font, std::string_view text,
                   const glm::vec2& position,
                   const glm::vec4& color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});
}

#endif //CPEN_RENDER_TEXT_HH
