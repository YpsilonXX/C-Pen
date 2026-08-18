#include "cpen/script/token.hh"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace cpen::script
{
    namespace
    {
        /// The enumerator names, in enumerator order — the dump's vocabulary.
        constexpr std::array<std::string_view, TOKEN_KIND_COUNT> TOKEN_KIND_NAMES = {
#define CPEN_TOKEN_KIND(name, description) #name,
#include "cpen/script/token_kinds.def"
        };

        /// The author-facing descriptions, in the same order.
        constexpr std::array<std::string_view, TOKEN_KIND_COUNT> TOKEN_KIND_DESCRIPTIONS = {
#define CPEN_TOKEN_KIND(name, description) description,
#include "cpen/script/token_kinds.def"
        };

        struct KeywordEntry
        {
            std::string_view text;
            TokenKind kind;
        };

        constexpr auto KEYWORDS = std::to_array<KeywordEntry>({
#define CPEN_KEYWORD(name, text) {text, TokenKind::name},
#include "cpen/script/token_kinds.def"
        });

        std::size_t index_of(const TokenKind kind) noexcept
        {
            const auto index = static_cast<std::size_t>(kind);
            return index < TOKEN_KIND_COUNT ? index : 0;
        }

        /// Re-escapes a chunk so one token always occupies one line of the dump.
        /// A literal containing a newline would otherwise silently split the
        /// output and make the golden file lie about how many tokens there are.
        std::string quote_for_dump(const std::string_view text)
        {
            std::string quoted;
            quoted.reserve(text.size() + 2);
            quoted.push_back('"');

            for (const char character : text)
            {
                switch (character)
                {
                    case '\\': quoted += "\\\\"; break;
                    case '"':  quoted += "\\\""; break;
                    case '\n': quoted += "\\n"; break;
                    case '\t': quoted += "\\t"; break;
                    case '\r': quoted += "\\r"; break;
                    default:   quoted.push_back(character); break;
                }
            }

            quoted.push_back('"');
            return quoted;
        }

        std::string describe_token_content(const TokenStream& stream, const Token& token)
        {
            if (token.kind == TokenKind::TEXT)
            {
                const TextLiteral* literal = stream.text_literal(token);
                return literal != nullptr ? dump_text_parts(literal->parts) : "<missing literal>";
            }

            const std::string_view lexeme = stream.lexeme(token);
            return lexeme.empty() ? std::string{} : std::format("'{}'", lexeme);
        }
    }

    std::string dump_text_parts(const std::vector<TextPart>& parts)
    {
        std::string rendered;

        for (const TextPart& part : parts)
        {
            if (!rendered.empty())
            {
                rendered.push_back(' ');
            }

            if (const auto* chunk = std::get_if<TextChunk>(&part))
            {
                rendered += quote_for_dump(chunk->text);
            }
            else
            {
                rendered += std::format("/{{{}}}", std::get<TextInterpolation>(part).variable);
            }
        }

        return rendered;
    }

    std::string_view to_string(const TokenKind kind) noexcept
    {
        return TOKEN_KIND_NAMES[index_of(kind)];
    }

    std::string_view describe(const TokenKind kind) noexcept
    {
        return TOKEN_KIND_DESCRIPTIONS[index_of(kind)];
    }

    std::optional<TokenKind> find_keyword(const std::string_view word) noexcept
    {
        const auto match = std::ranges::find(KEYWORDS, word, &KeywordEntry::text);
        return match != KEYWORDS.end() ? std::optional(match->kind) : std::nullopt;
    }

    bool is_keyword(const TokenKind kind) noexcept
    {
        return std::ranges::find(KEYWORDS, kind, &KeywordEntry::kind) != KEYWORDS.end();
    }

    std::string_view TokenStream::lexeme(const Token& token) const noexcept
    {
        if (token.span.offset >= this->content.size())
        {
            return {};
        }

        return std::string_view(this->content).substr(token.span.offset, token.span.length);
    }

    const TextLiteral* TokenStream::text_literal(const Token& token) const noexcept
    {
        if (token.kind != TokenKind::TEXT || token.payload >= this->text_literals.size())
        {
            return nullptr;
        }

        return &this->text_literals[token.payload];
    }

    std::uint32_t TokenStream::add_text_literal(TextLiteral literal)
    {
        this->text_literals.push_back(std::move(literal));
        return static_cast<std::uint32_t>(this->text_literals.size() - 1);
    }

    std::string dump_tokens(const TokenStream& stream)
    {
        std::string dumped;

        for (const Token& token : stream.tokens())
        {
            const LineColumn position = locate(stream.source(), token.span.offset);
            const std::string content = describe_token_content(stream, token);

            if (content.empty())
            {
                dumped += std::format("{:>4}:{:<4} {}\n",
                                      position.line, position.column, to_string(token.kind));
            }
            else
            {
                dumped += std::format("{:>4}:{:<4} {:<20} {}\n",
                                      position.line, position.column,
                                      to_string(token.kind), content);
            }
        }

        return dumped;
    }
}
