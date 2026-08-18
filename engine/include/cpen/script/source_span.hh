#ifndef CPEN_SCRIPT_SOURCE_SPAN_HH
#define CPEN_SCRIPT_SOURCE_SPAN_HH

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace cpen::script
{
    /// Where something is in a source file, as a range of bytes.
    ///
    /// Carried by every token and every syntax-tree node from the first line of
    /// the front end onwards. Retrofitting spans into a parser that did without
    /// them means touching every node constructor and every reporting site at
    /// once; carrying them from the start costs two integers per node.
    ///
    /// Byte offsets rather than line and column numbers. Lines and columns are
    /// derived on demand by locate(), because they are needed only when something
    /// is shown to a human, and because a stored line number is one more thing
    /// that can quietly disagree with the text it describes.
    struct SourceSpan
    {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;

        constexpr std::uint32_t end() const noexcept
        {
            return this->offset + this->length;
        }

        bool operator==(const SourceSpan& other) const noexcept = default;
    };

    /// The smallest span covering both, for a node that reports its own extent
    /// from those of its children ("this whole expression").
    constexpr SourceSpan merge(const SourceSpan first, const SourceSpan second) noexcept
    {
        const std::uint32_t begin = std::min(first.offset, second.offset);
        const std::uint32_t finish = std::max(first.end(), second.end());

        return SourceSpan{.offset = begin, .length = finish - begin};
    }

    /// A position as a human reads it: both numbered from one, as every editor
    /// numbers them.
    struct LineColumn
    {
        std::uint32_t line = 1;
        std::uint32_t column = 1;
    };

    /// Converts a byte offset into a line and a column.
    ///
    /// The column counts code points, not bytes. A line of Cyrillic would
    /// otherwise be reported at roughly twice the column the author's editor
    /// shows, which makes the position worse than useless — it looks precise and
    /// points at the wrong character.
    ///
    /// Scans from the beginning of the file, so the cost is linear in the offset.
    /// Deliberate: this runs once per reported diagnostic and never inside the
    /// lexer's loop, and it leaves no cached position that could desynchronise
    /// from the text.
    LineColumn locate(std::string_view source, std::uint32_t offset) noexcept;

    /// The whole line containing `offset`, without its terminator — what a
    /// diagnostic quotes back to the reader.
    std::string_view extract_line(std::string_view source, std::uint32_t offset) noexcept;
}

#endif //CPEN_SCRIPT_SOURCE_SPAN_HH
