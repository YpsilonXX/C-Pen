#include "cpen/script/source_span.hh"

#include "cpen/core/utf8.hh"

#include <cstddef>

namespace cpen::script
{
    LineColumn locate(const std::string_view source, const std::uint32_t offset) noexcept
    {
        const std::size_t limit = std::min(static_cast<std::size_t>(offset), source.size());

        LineColumn position;
        std::size_t index = 0;

        while (index < limit)
        {
            if (source[index] == '\n')
            {
                ++position.line;
                position.column = 1;
                ++index;
                continue;
            }

            const core::DecodedCodePoint decoded = core::decode_utf8(source, index);

            // A malformed byte decodes to the replacement character and reports a
            // size of one, so the loop advances on any input. The guard is for the
            // end-of-input case, where the reported size is zero.
            index += decoded.size > 0 ? decoded.size : 1;
            ++position.column;
        }

        return position;
    }

    std::string_view extract_line(const std::string_view source, const std::uint32_t offset) noexcept
    {
        const std::size_t position = std::min(static_cast<std::size_t>(offset), source.size());

        std::size_t begin = 0;
        if (position > 0)
        {
            // Searched from one byte back: an offset sitting exactly on a newline
            // belongs to the line that newline ends, not to the next one.
            const std::size_t previous = source.rfind('\n', position - 1);
            if (previous != std::string_view::npos)
            {
                begin = previous + 1;
            }
        }

        std::size_t finish = source.find('\n', position);
        if (finish == std::string_view::npos)
        {
            finish = source.size();
        }

        // Carriage returns are normalised away before lexing, so this only matters
        // for a caller quoting text that never went through the lexer.
        if (finish > begin && source[finish - 1] == '\r')
        {
            --finish;
        }

        return source.substr(begin, finish - begin);
    }
}
