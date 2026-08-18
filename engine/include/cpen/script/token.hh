#ifndef CPEN_SCRIPT_TOKEN_HH
#define CPEN_SCRIPT_TOKEN_HH

#include "cpen/script/source_span.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cpen::script
{
    /// Every kind of token, generated from token_kinds.def. See that file before
    /// adding one.
    enum class TokenKind : std::uint8_t
    {
#define CPEN_TOKEN_KIND(name, description) name,
#include "cpen/script/token_kinds.def"
    };

    inline constexpr std::size_t TOKEN_KIND_COUNT = 0
#define CPEN_TOKEN_KIND(name, description) + 1
#include "cpen/script/token_kinds.def"
        ;

    /// The enumerator's own name ("COLON"). What a token dump prints, and
    /// therefore what a golden test compares.
    std::string_view to_string(TokenKind kind) noexcept;

    /// How the token is named to the author ("':'", "identifier"), for messages
    /// of the form "expected ':' after the label name".
    std::string_view describe(TokenKind kind) noexcept;

    /// The keyword spelled exactly like `word`, if there is one. Identifiers are
    /// scanned first and looked up here afterwards, so a keyword is never a
    /// prefix problem: "labels" is an identifier, not `label` followed by "s".
    std::optional<TokenKind> find_keyword(std::string_view word) noexcept;

    /// Whether the kind is one of the keywords rather than punctuation or a
    /// literal. Used by the parser's statement table and by tooling.
    bool is_keyword(TokenKind kind) noexcept;

    /// A run of ordinary characters inside a text literal, with escape sequences
    /// already resolved.
    struct TextChunk
    {
        std::string text;
    };

    /// A `/{name}` interpolation inside a text literal.
    ///
    /// Only a variable name is allowed between the braces — computing something
    /// belongs on a `$` line above, where it is visible to anyone skimming the
    /// script for logic. The span covers the name alone, so a diagnostic about an
    /// unknown variable points at the name and not at the whole line.
    struct TextInterpolation
    {
        std::string variable;
        SourceSpan span{};
    };

    using TextPart = std::variant<TextChunk, TextInterpolation>;

    /// A text literal, resolved into the pieces it will be assembled from.
    ///
    /// Splitting happens in the lexer because escapes and interpolation are
    /// lexical rules, and because it lets the parser treat a literal as one
    /// value. Adjacent ordinary characters are merged into a single chunk, so a
    /// literal with no interpolation holds exactly one part.
    struct TextLiteral
    {
        std::vector<TextPart> parts;
    };

    /// One token: what it is and where it came from.
    ///
    /// Deliberately small and copyable, with no decoded value inside. Numbers
    /// are converted where they become syntax-tree nodes, since the range a
    /// literal has to fit is a property of the value model rather than of the
    /// text; text literals are the one exception, and are held in a side table
    /// the token indexes.
    struct Token
    {
        TokenKind kind = TokenKind::END_OF_FILE;
        SourceSpan span{};

        /// Meaning depends on the kind: for TEXT, the index of the resolved
        /// literal in the stream's table. Unused, and zero, for everything else.
        std::uint32_t payload = 0;
    };

    /// The lexer's output: the tokens, the text they were read from, and the
    /// literals they refer to.
    ///
    /// The source is owned rather than borrowed. Every span, every lexeme and
    /// every diagnostic points into it, and the text the lexer worked on is not
    /// quite the text on disk anyway — a byte-order mark is dropped and CRLF line
    /// endings are normalised before scanning, so the stream is the only copy for
    /// which the recorded offsets are true.
    class TokenStream
    {
    public:
        TokenStream() = default;

        TokenStream(std::string source_name, std::string source)
            : file_name(std::move(source_name)),
              content(std::move(source))
        {
        }

        std::string_view source_name() const noexcept { return this->file_name; }
        std::string_view source() const noexcept { return this->content; }
        const std::vector<Token>& tokens() const noexcept { return this->token_list; }

        /// The exact characters the token was made of. Empty for the zero-length
        /// structural tokens, which is what the dump relies on to omit them.
        std::string_view lexeme(const Token& token) const noexcept;

        /// The resolved literal a TEXT token refers to, or nullptr for any other
        /// kind — a null return is a misuse rather than a condition to handle.
        const TextLiteral* text_literal(const Token& token) const noexcept;

        void append(const Token token) { this->token_list.push_back(token); }

        std::uint32_t add_text_literal(TextLiteral literal);

    private:
        std::string file_name;
        std::string content;
        std::vector<Token> token_list;
        std::vector<TextLiteral> text_literals;
    };

    /// Renders resolved literal parts the way a dump shows them: chunks quoted
    /// and re-escaped so that one token stays on one line, interpolations written
    /// back as /{name}. Shared with the syntax-tree dump, which prints the same
    /// parts inside a text node.
    std::string dump_text_parts(const std::vector<TextPart>& parts);

    /// Renders the stream as one line per token: position, kind, and what it was
    /// made of.
    ///
    ///        1:1  LABEL                'label'
    ///        1:7  IDENTIFIER           'start'
    ///        2:2  TEXT                 "У тебя " /{sympathy} " очков."
    ///
    /// This exists to be compared against an expected string in a test. A change
    /// to a lexical rule then shows up as a diff over every affected case at once,
    /// instead of as a rewrite of a dozen hand-written assertions — which is the
    /// difference between a language that can still be changed and one that can
    /// only be extended.
    std::string dump_tokens(const TokenStream& stream);
}

#endif //CPEN_SCRIPT_TOKEN_HH
