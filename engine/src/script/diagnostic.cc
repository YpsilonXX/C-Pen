#include "cpen/script/diagnostic.hh"

#include "cpen/core/utf8.hh"

#include <algorithm>
#include <cstddef>

namespace cpen::script
{
    namespace
    {
        /// The caret line under a quoted source line.
        ///
        /// Built by walking the real characters of the line rather than by
        /// repeating spaces: whitespace is copied as itself so tabs keep their
        /// width, and every other code point contributes exactly one space, so
        /// Cyrillic text lines up as it does on screen.
        std::string build_caret_line(const std::string_view line,
                                     const std::uint32_t column,
                                     const std::uint32_t span_length)
        {
            std::string caret;
            std::size_t index = 0;
            std::uint32_t current_column = 1;

            while (index < line.size() && current_column < column)
            {
                caret.push_back(line[index] == '\t' ? '\t' : ' ');

                const core::DecodedCodePoint decoded = core::decode_utf8(line, index);
                index += decoded.size > 0 ? decoded.size : 1;
                ++current_column;
            }

            caret.push_back('^');

            // Underline the rest of the span, counted in code points and clamped to
            // the line: a span may legitimately end past it (an unterminated text
            // literal runs to the end of the input).
            std::uint32_t remaining = span_length > 0 ? span_length - 1 : 0;
            while (index < line.size() && remaining > 0)
            {
                caret.push_back('~');

                const core::DecodedCodePoint decoded = core::decode_utf8(line, index);
                index += decoded.size > 0 ? decoded.size : 1;
                --remaining;
            }

            return caret;
        }
    }

    bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept
    {
        return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic)
        {
            return diagnostic.severity == Severity::ERROR;
        });
    }

    std::string render_diagnostic(const Diagnostic& diagnostic,
                                  const std::string_view source,
                                  const std::string_view source_name)
    {
        const LineColumn position = locate(source, diagnostic.span.offset);
        const std::string_view line = extract_line(source, diagnostic.span.offset);

        return std::format("{}:{}:{}: {}: {}\n{}\n{}\n",
                           source_name, position.line, position.column,
                           to_string(diagnostic.severity), diagnostic.message,
                           line,
                           build_caret_line(line, position.column, diagnostic.span.length));
    }

    std::string render_diagnostics(const std::vector<Diagnostic>& diagnostics,
                                   const std::string_view source,
                                   const std::string_view source_name)
    {
        std::string rendered;
        for (const Diagnostic& diagnostic : diagnostics)
        {
            rendered += render_diagnostic(diagnostic, source, source_name);
        }

        return rendered;
    }
}
