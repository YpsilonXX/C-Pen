#ifndef CPEN_SCRIPT_LEXER_HH
#define CPEN_SCRIPT_LEXER_HH

#include "cpen/script/diagnostic.hh"
#include "cpen/script/token.hh"

#include <string>
#include <vector>

namespace cpen::script
{
    /// What one run of the lexer produced.
    ///
    /// A pair rather than std::expected, unlike the rest of the engine, because
    /// the two halves are not alternatives: a file with a bad escape in it still
    /// has a usable token stream, and reporting only the first problem in a script
    /// would make fixing one a round trip per mistake. The compiler collapses the
    /// list into std::expected at its own boundary, where a caller really does
    /// either get a chunk or get the reasons it did not.
    struct TokenizeResult
    {
        TokenStream stream;
        std::vector<Diagnostic> diagnostics;

        /// Whether anything found makes the stream unfit to compile. Warnings on
        /// their own do not.
        bool failed() const noexcept { return has_errors(this->diagnostics); }
    };

    /// Turns script source into tokens.
    ///
    /// The text is taken by value and kept: a byte-order mark is removed and CRLF
    /// line endings are normalised to LF before scanning, so the recorded offsets
    /// are true of the stream's copy and not of the file on disk. Line numbers are
    /// unaffected by both.
    ///
    /// Scanning never stops at the first mistake. Every rule that can fail has a
    /// defined recovery — an unknown escape stands for the character it precedes,
    /// an unterminated literal ends at the line, an unexpected character is
    /// skipped — so that one run reports everything wrong with the file.
    ///
    /// Indentation is significant, and the rule is that a file must be consistent
    /// with itself: the first indented line decides whether the file is written
    /// with tabs or with spaces, and every later indent must use that same
    /// character. Neither choice is privileged. A tab-only language would break
    /// authors whose editor inserts spaces when they press Tab, and a space-only
    /// one forces the opposite; requiring homogeneity costs the lexer one
    /// remembered character and lets both work, while still leaving the width of
    /// an indent level unambiguous — within one file, comparing counts of the same
    /// character is exact.
    TokenizeResult tokenize(std::string source_name, std::string source);
}

#endif //CPEN_SCRIPT_LEXER_HH
