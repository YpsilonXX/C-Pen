#include "cpen/render/text.hh"

#include "cpen/core/utf8.hh"
#include "cpen/render/font.hh"
#include "cpen/render/sprite.hh"
#include "cpen/render/sprite_batch.hh"

#include <algorithm>
#include <cstddef>

namespace cpen::render
{
    namespace
    {
        constexpr char32_t NEWLINE = U'\n';
        constexpr char32_t SPACE = U' ';
        constexpr char32_t CARRIAGE_RETURN = U'\r';

        /// A line of the wrapped output, as offsets into the original text.
        struct LineBounds
        {
            std::size_t begin = 0;
            std::size_t end = 0;
        };

        /// The text between two offsets, without the carriage return that a file
        /// saved on Windows leaves in front of every newline.
        ///
        /// Trimmed here rather than skipped while scanning, because the scan
        /// produces offsets and the carriage return sits inside the range they
        /// delimit. Leaving it in would give a line that measures and draws
        /// correctly — both of those ignore it — while comparing unequal to the
        /// same line read from a file saved anywhere else.
        std::string_view slice(const std::string_view text, const LineBounds& bounds)
        {
            std::string_view line = text.substr(bounds.begin, bounds.end - bounds.begin);

            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }

            return line;
        }

        /// The advance of one code point of `font`, or zero if it has no glyph.
        float advance_of_glyph(Font& font, const char32_t code_point)
        {
            const Glyph* const glyph = font.glyph(code_point);
            return glyph != nullptr ? glyph->advance : 0.0f;
        }
    }

    std::vector<std::string_view> wrap_text(const std::string_view text,
                                            const float maximum_width,
                                            const AdvanceFunction& advance_of)
    {
        std::vector<std::string_view> lines;

        if (text.empty())
        {
            return lines;
        }

        const bool wrapping = maximum_width > 0.0f;

        std::size_t line_begin = 0;
        float line_width = 0.0f;

        // Where a break may be taken, and where the line after it resumes. The two
        // straddle a whole run of spaces rather than one: the run belongs to
        // neither line, so the break goes in front of the first space and the next
        // line starts after the last. Recording the latest space instead would
        // leave every space but one hanging off the end of the line above.
        bool has_break_point = false;
        bool in_space_run = false;
        std::size_t break_at = 0;
        std::size_t resume_at = 0;

        std::size_t offset = 0;

        while (offset < text.size())
        {
            const core::DecodedCodePoint decoded = core::decode_utf8(text, offset);
            const std::size_t next = offset + decoded.size;

            if (decoded.code_point == NEWLINE)
            {
                lines.push_back(slice(text, LineBounds{.begin = line_begin, .end = offset}));

                line_begin = next;
                line_width = 0.0f;
                has_break_point = false;
                in_space_run = false;
                offset = next;
                continue;
            }

            // Carriage returns arrive in text saved on Windows and are not
            // characters to lay out. Skipped rather than measured, so a file's line
            // endings cannot change where it wraps.
            if (decoded.code_point == CARRIAGE_RETURN)
            {
                offset = next;
                continue;
            }

            const float advance = advance_of(decoded.code_point);

            const bool is_space = decoded.code_point == SPACE;

            if (is_space && !in_space_run)
            {
                has_break_point = true;
                break_at = offset;
            }

            // Kept up to date through the whole run, so the next line begins at the
            // first character that is not a space.
            if (is_space)
            {
                resume_at = next;
            }

            in_space_run = is_space;

            // A space never forces a break of its own: a line whose overflow is
            // nothing but trailing spaces has not really overflowed, and breaking
            // on one would start the next line with the word that followed it
            // anyway.
            if (!is_space && wrapping && line_width + advance > maximum_width &&
                offset > line_begin)
            {
                if (has_break_point)
                {
                    lines.push_back(
                        slice(text, LineBounds{.begin = line_begin, .end = break_at}));
                    line_begin = resume_at;
                }
                else
                {
                    // One word wider than the whole line. Broken here rather than
                    // allowed to run past the edge, where no amount of rewrapping
                    // would ever bring it back.
                    lines.push_back(
                        slice(text, LineBounds{.begin = line_begin, .end = offset}));
                    line_begin = offset;
                }

                has_break_point = false;

                // Remeasure the tail of the line that was carried over, since the
                // break may have been several characters back.
                line_width = 0.0f;
                for (std::size_t scan = line_begin; scan < offset;)
                {
                    const core::DecodedCodePoint carried = core::decode_utf8(text, scan);
                    line_width += advance_of(carried.code_point);
                    scan += carried.size;
                }
            }

            line_width += advance;
            offset = next;
        }

        lines.push_back(slice(text, LineBounds{.begin = line_begin, .end = text.size()}));

        return lines;
    }

    std::vector<std::string_view> wrap_text(Font& font, const std::string_view text,
                                            const float maximum_width)
    {
        return wrap_text(text, maximum_width,
                         [&font](const char32_t code_point)
                         { return advance_of_glyph(font, code_point); });
    }

    glm::vec2 measure_text(Font& font, const std::string_view text)
    {
        float widest = 0.0f;
        float current = 0.0f;
        std::size_t line_count = 1;

        std::size_t offset = 0;
        while (offset < text.size())
        {
            const core::DecodedCodePoint decoded = core::decode_utf8(text, offset);
            offset += decoded.size;

            if (decoded.code_point == NEWLINE)
            {
                widest = std::max(widest, current);
                current = 0.0f;
                ++line_count;
                continue;
            }

            if (decoded.code_point == CARRIAGE_RETURN)
            {
                continue;
            }

            current += advance_of_glyph(font, decoded.code_point);
        }

        widest = std::max(widest, current);

        // A whole number of line heights rather than the ink's own extent: two
        // labels of different letters must line up, and the height of a box is a
        // property of the typeface rather than of what happens to be written in it.
        return glm::vec2{widest, static_cast<float>(line_count) * font.line_height()};
    }

    void draw_text(SpriteBatch& batch, Font& font, const std::string_view text,
                   const glm::vec2& position, const glm::vec4& color)
    {
        // The pen sits on the baseline; the caller gave the top of the line box.
        glm::vec2 pen{position.x, position.y + font.ascender()};

        std::size_t offset = 0;
        while (offset < text.size())
        {
            const core::DecodedCodePoint decoded = core::decode_utf8(text, offset);
            offset += decoded.size;

            if (decoded.code_point == NEWLINE)
            {
                pen.x = position.x;
                pen.y += font.line_height();
                continue;
            }

            if (decoded.code_point == CARRIAGE_RETURN)
            {
                continue;
            }

            const Glyph* const glyph = font.glyph(decoded.code_point);
            if (glyph == nullptr)
            {
                continue;
            }

            // A space, and anything else with no ink, moves the pen and draws
            // nothing. Submitting it would cost an instance for an empty quad.
            if (glyph->size.x > 0.0f && glyph->size.y > 0.0f)
            {
                batch.draw(font.atlas(), Sprite{
                                             .position = pen + glyph->bearing,
                                             .size = glyph->size,
                                             .region = glyph->region,
                                             .color = color,
                                         });
            }

            pen.x += glyph->advance;
        }
    }
}
