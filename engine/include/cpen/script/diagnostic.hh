#ifndef CPEN_SCRIPT_DIAGNOSTIC_HH
#define CPEN_SCRIPT_DIAGNOSTIC_HH

#include "cpen/script/source_span.hh"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cpen::script
{
    /// How much one diagnostic matters.
    ///
    /// Note for Windows builds: <wingdi.h> defines a macro named ERROR, exactly as
    /// core/log.hh warns for its own Level enum. A translation unit that includes
    /// both must define NOGDI, or #undef ERROR, before this header.
    enum class Severity : std::uint8_t
    {
        NOTE,
        WARNING,
        ERROR,
    };

    constexpr std::string_view to_string(const Severity severity) noexcept
    {
        switch (severity)
        {
            case Severity::NOTE:    return "note";
            case Severity::WARNING: return "warning";
            case Severity::ERROR:   return "error";
        }
        return "unknown";
    }

    /// One thing the front end has to say about the script it was given.
    ///
    /// Deliberately not core::Error. That type answers "why did this call fail",
    /// one failure per call; a compiler answers "what is wrong with this file",
    /// which is a list, and every entry of it has to point at a place in the text.
    struct Diagnostic
    {
        Severity severity = Severity::ERROR;
        SourceSpan span{};
        std::string message;
    };

    /// Builds a Diagnostic with a formatted message, so a reporting site is one
    /// call rather than a designated initialiser wrapped around std::format.
    template <typename... Arguments>
    Diagnostic make_diagnostic(const Severity severity,
                               const SourceSpan span,
                               const std::format_string<Arguments...> format,
                               Arguments&&... arguments)
    {
        return Diagnostic{
            .severity = severity,
            .span = span,
            .message = std::format(format, std::forward<Arguments>(arguments)...),
        };
    }

    /// Whether anything in the list stops compilation. Each phase produces its
    /// diagnostics and lets the caller ask this, rather than returning a bare
    /// success flag that throws the messages away.
    bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept;

    /// Renders one diagnostic the way a compiler does: position, severity,
    /// message, the offending line, and a caret under the span.
    ///
    ///     chapter1.pen:4:12: error: unknown escape sequence '\q'
    ///         alice "\qwe"
    ///                ^~
    ///
    /// The indentation of the quoted line is copied verbatim into the caret line
    /// rather than replaced by spaces, so a tab-indented script still lines up
    /// whatever tab width the reader's terminal uses.
    std::string render_diagnostic(const Diagnostic& diagnostic,
                                  std::string_view source,
                                  std::string_view source_name);

    /// Renders every diagnostic in order, one after another.
    std::string render_diagnostics(const std::vector<Diagnostic>& diagnostics,
                                   std::string_view source,
                                   std::string_view source_name);
}

#endif //CPEN_SCRIPT_DIAGNOSTIC_HH
