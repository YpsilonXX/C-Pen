#include "cpen/script/lexer.hh"

#include "cpen/core/utf8.hh"

#include <cstddef>
#include <string_view>
#include <utility>

namespace cpen::script
{
    namespace
    {
        constexpr bool is_ascii_digit(const char character) noexcept
        {
            return character >= '0' && character <= '9';
        }

        /// What may begin a name.
        ///
        /// Every byte at or above 0x80 counts, which is the whole of the language's
        /// Unicode support in identifiers: a UTF-8 continuation or lead byte is
        /// never an operator, a digit or a delimiter, so accepting the lot lets
        /// `$ симпатия = 0` work without carrying a single character-classification
        /// table. The cost is that a malformed byte sequence becomes part of a name
        /// instead of being reported, and it is then reported by whatever compares
        /// that name with another.
        constexpr bool is_identifier_start(const char character) noexcept
        {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || character == '_'
                || static_cast<unsigned char>(character) >= 0x80;
        }

        constexpr bool is_identifier_continuation(const char character) noexcept
        {
            return is_identifier_start(character) || is_ascii_digit(character);
        }

        constexpr bool is_indentation(const char character) noexcept
        {
            return character == ' ' || character == '\t';
        }

        constexpr std::string_view name_of_indentation(const char character) noexcept
        {
            return character == '\t' ? "tabs" : "spaces";
        }

        /// Removes what an editor added and the language does not want to see.
        ///
        /// A byte-order mark at the start of a UTF-8 file is legal and meaningless,
        /// and would otherwise be lexed as the beginning of an identifier. CRLF
        /// endings are normalised so that every later rule can test for '\n' alone;
        /// a stray carriage return in the middle of a line is left as it is, and
        /// will be reported as an unexpected character.
        std::string normalise(std::string source)
        {
            constexpr std::string_view BYTE_ORDER_MARK = "\xEF\xBB\xBF";
            if (source.starts_with(BYTE_ORDER_MARK))
            {
                source.erase(0, BYTE_ORDER_MARK.size());
            }

            std::string normalised;
            normalised.reserve(source.size());

            for (std::size_t index = 0; index < source.size(); ++index)
            {
                if (source[index] == '\r' && index + 1 < source.size() && source[index + 1] == '\n')
                {
                    continue;
                }

                normalised.push_back(source[index]);
            }

            return normalised;
        }

        class Lexer
        {
        public:
            Lexer(std::string source_name, std::string source)
                : stream(std::move(source_name), normalise(std::move(source))),
                  text(this->stream.source())
            {
            }

            TokenizeResult run()
            {
                while (!this->at_end())
                {
                    this->scan_line();
                }

                // Every block the file opened is closed at the end of it, so the
                // parser never has to treat the end of input as a special way for a
                // block to finish.
                const SourceSpan end{.offset = static_cast<std::uint32_t>(this->text.size()), .length = 0};
                while (this->indent_stack.size() > 1)
                {
                    this->indent_stack.pop_back();
                    this->emit(TokenKind::DEDENT, end);
                }

                this->emit(TokenKind::END_OF_FILE, end);

                return TokenizeResult{
                    .stream = std::move(this->stream),
                    .diagnostics = std::move(this->diagnostics),
                };
            }

        private:
            bool at_end() const noexcept { return this->position >= this->text.size(); }

            char peek(const std::size_t lookahead = 0) const noexcept
            {
                const std::size_t index = this->position + lookahead;
                return index < this->text.size() ? this->text[index] : '\0';
            }

            void advance() noexcept { ++this->position; }

            std::uint32_t offset() const noexcept
            {
                return static_cast<std::uint32_t>(this->position);
            }

            SourceSpan span_from(const std::size_t start) const noexcept
            {
                return SourceSpan{
                    .offset = static_cast<std::uint32_t>(start),
                    .length = static_cast<std::uint32_t>(this->position - start),
                };
            }

            /// The bytes of the code point at the current position, so a message
            /// naming an offending character does not cut a UTF-8 sequence in half.
            std::string_view current_code_point() const noexcept
            {
                const core::DecodedCodePoint decoded = core::decode_utf8(this->text, this->position);
                const std::size_t size = decoded.size > 0 ? decoded.size : 1;

                return this->text.substr(this->position, size);
            }

            void emit(const TokenKind kind, const SourceSpan span, const std::uint32_t payload = 0)
            {
                this->stream.append(Token{.kind = kind, .span = span, .payload = payload});
            }

            template <typename... Arguments>
            void error(const SourceSpan span,
                       const std::format_string<Arguments...> format,
                       Arguments&&... arguments)
            {
                this->diagnostics.push_back(make_diagnostic(Severity::ERROR, span, format,
                                                            std::forward<Arguments>(arguments)...));
            }

            void skip_to_end_of_line() noexcept
            {
                while (!this->at_end() && this->peek() != '\n')
                {
                    this->advance();
                }
            }

            void scan_line()
            {
                const std::size_t line_start = this->position;

                char indentation = '\0';
                bool mixed = false;
                std::uint32_t width = 0;

                while (is_indentation(this->peek()))
                {
                    if (indentation == '\0')
                    {
                        indentation = this->peek();
                    }
                    else if (this->peek() != indentation)
                    {
                        mixed = true;
                    }

                    ++width;
                    this->advance();
                }

                // A line with nothing on it but whitespace, or nothing but a
                // comment, has no indentation as far as the block structure is
                // concerned. Otherwise a blank line left at the wrong depth by an
                // editor would close blocks that the author still considers open,
                // and the invisible characters on it would have to obey the
                // homogeneity rule to boot.
                if (this->at_end() || this->peek() == '\n' || this->peek() == '#')
                {
                    this->skip_to_end_of_line();
                    if (!this->at_end())
                    {
                        this->advance();
                    }
                    return;
                }

                this->handle_indentation(line_start, width, indentation, mixed);

                bool produced_token = false;
                while (!this->at_end() && this->peek() != '\n' && this->peek() != '#')
                {
                    if (is_indentation(this->peek()))
                    {
                        this->advance();
                        continue;
                    }

                    produced_token |= this->scan_token();
                }

                this->skip_to_end_of_line();

                // The newline token stands where the line ends, whether that is a
                // newline character or the end of the file. A line that produced
                // nothing but diagnostics gets none: the parser would otherwise see
                // an empty statement and report a second, invented problem.
                if (produced_token)
                {
                    this->emit(TokenKind::NEWLINE, SourceSpan{.offset = this->offset(), .length = 0});
                }

                if (!this->at_end())
                {
                    this->advance();
                }
            }

            void handle_indentation(const std::size_t line_start,
                                    const std::uint32_t width,
                                    const char indentation,
                                    const bool mixed)
            {
                const SourceSpan whitespace{
                    .offset = static_cast<std::uint32_t>(line_start),
                    .length = width,
                };

                if (width > 0)
                {
                    if (this->indent_character == '\0')
                    {
                        this->indent_character = indentation;
                        this->indent_origin = whitespace;
                    }

                    // Reported once per file rather than once per line. A file
                    // converted wholesale by an editor would otherwise produce one
                    // error per indented line, burying every other diagnostic in
                    // the run.
                    if (!this->reported_indentation_mismatch && (mixed || indentation != this->indent_character))
                    {
                        this->reported_indentation_mismatch = true;

                        const LineColumn origin = locate(this->text, this->indent_origin.offset);

                        if (mixed)
                        {
                            this->error(whitespace,
                                        "this line's indentation mixes tabs and spaces; "
                                        "the file is indented with {} (first indent on line {})",
                                        name_of_indentation(this->indent_character), origin.line);
                        }
                        else
                        {
                            this->error(whitespace,
                                        "this line is indented with {}, but the file is indented "
                                        "with {} (first indent on line {})",
                                        name_of_indentation(indentation),
                                        name_of_indentation(this->indent_character), origin.line);
                        }
                    }
                }

                const SourceSpan marker{.offset = this->offset(), .length = 0};

                if (width > this->indent_stack.back())
                {
                    this->indent_stack.push_back(width);
                    this->emit(TokenKind::INDENT, marker);
                    return;
                }

                while (width < this->indent_stack.back())
                {
                    this->indent_stack.pop_back();
                    this->emit(TokenKind::DEDENT, marker);
                }

                if (width != this->indent_stack.back())
                {
                    this->error(whitespace,
                                "this line is indented {} deep, which matches no enclosing block "
                                "(the nearest are {} and {})",
                                width, this->indent_stack.back(),
                                this->indent_stack.size() > 1
                                    ? this->indent_stack[this->indent_stack.size() - 2]
                                    : 0);

                    // Adopted as the current level so that the blocks the rest of
                    // the file opens still close in pairs.
                    this->indent_stack.back() = width;
                }
            }

            /// Scans one token. Returns whether a token was produced: an
            /// unrecognised character is consumed and reported instead.
            bool scan_token()
            {
                const std::size_t start = this->position;
                const char character = this->peek();

                if (character == '"')
                {
                    this->scan_text_literal();
                    return true;
                }

                if (is_ascii_digit(character))
                {
                    this->scan_number();
                    return true;
                }

                if (is_identifier_start(character))
                {
                    this->scan_identifier();
                    return true;
                }

                return this->scan_operator(start, character);
            }

            bool scan_operator(const std::size_t start, const char character)
            {
                const auto single = [&](const TokenKind kind)
                {
                    this->advance();
                    this->emit(kind, this->span_from(start));
                    return true;
                };

                const auto followed_by_equals = [&](const TokenKind with_equals, const TokenKind alone)
                {
                    this->advance();
                    if (this->peek() == '=')
                    {
                        this->advance();
                        this->emit(with_equals, this->span_from(start));
                    }
                    else
                    {
                        this->emit(alone, this->span_from(start));
                    }
                    return true;
                };

                switch (character)
                {
                    case '$': return single(TokenKind::DOLLAR);
                    case ':': return single(TokenKind::COLON);
                    case ',': return single(TokenKind::COMMA);
                    case '(': return single(TokenKind::LEFT_PARENTHESIS);
                    case ')': return single(TokenKind::RIGHT_PARENTHESIS);
                    case '+': return single(TokenKind::PLUS);
                    case '-': return single(TokenKind::MINUS);
                    case '*': return single(TokenKind::STAR);
                    case '/': return single(TokenKind::SLASH);
                    case '%': return single(TokenKind::PERCENT);
                    case '=': return followed_by_equals(TokenKind::EQUAL_EQUAL, TokenKind::EQUAL);
                    case '<': return followed_by_equals(TokenKind::LESS_EQUAL, TokenKind::LESS);
                    case '>': return followed_by_equals(TokenKind::GREATER_EQUAL, TokenKind::GREATER);

                    case '!':
                        if (this->peek(1) == '=')
                        {
                            return followed_by_equals(TokenKind::NOT_EQUAL, TokenKind::NOT_EQUAL);
                        }

                        this->advance();
                        this->error(this->span_from(start),
                                    "'!' is not an operator here; negation is written 'not'");
                        return false;

                    default:
                    {
                        const std::string_view unexpected = this->current_code_point();
                        this->position += unexpected.size();
                        this->error(this->span_from(start), "unexpected character '{}'", unexpected);
                        return false;
                    }
                }
            }

            void scan_identifier()
            {
                const std::size_t start = this->position;
                while (is_identifier_continuation(this->peek()))
                {
                    this->advance();
                }

                const SourceSpan span = this->span_from(start);
                const std::string_view word = this->text.substr(start, span.length);

                this->emit(find_keyword(word).value_or(TokenKind::IDENTIFIER), span);
            }

            /// Scans a number literal, delimiting it without converting it.
            ///
            /// What the digits mean — whether they fit in an integer, how the
            /// fraction rounds — is a question about the value model, and is
            /// answered where the literal becomes a syntax-tree node. The lexer's
            /// job is to say where the literal ends.
            void scan_number()
            {
                const std::size_t start = this->position;
                while (is_ascii_digit(this->peek()))
                {
                    this->advance();
                }

                if (this->peek() != '.')
                {
                    this->emit(TokenKind::INTEGER, this->span_from(start));
                    return;
                }

                const std::size_t point = this->position;
                this->advance();

                if (!is_ascii_digit(this->peek()))
                {
                    this->error(SourceSpan{.offset = static_cast<std::uint32_t>(point), .length = 1},
                                "a decimal point must be followed by a digit");
                }

                while (is_ascii_digit(this->peek()))
                {
                    this->advance();
                }

                this->emit(TokenKind::FLOATING, this->span_from(start));
            }

            void scan_text_literal()
            {
                const std::size_t start = this->position;
                this->advance();

                TextLiteral literal;
                std::string chunk;
                bool terminated = false;

                const auto flush = [&]
                {
                    if (!chunk.empty())
                    {
                        literal.parts.emplace_back(TextChunk{.text = std::move(chunk)});
                        chunk.clear();
                    }
                };

                while (!this->at_end() && this->peek() != '\n')
                {
                    if (this->peek() == '"')
                    {
                        this->advance();
                        terminated = true;
                        break;
                    }

                    if (this->peek() == '\\')
                    {
                        this->scan_escape(chunk);
                        continue;
                    }

                    if (this->peek() == '/' && this->peek(1) == '{')
                    {
                        flush();
                        this->scan_interpolation(literal);
                        continue;
                    }

                    chunk.push_back(this->peek());
                    this->advance();
                }

                flush();

                // An empty literal still holds one part, so that every literal has
                // a value and the dump shows "" rather than nothing at all.
                if (literal.parts.empty())
                {
                    literal.parts.emplace_back(TextChunk{});
                }

                const SourceSpan span = this->span_from(start);

                if (!terminated)
                {
                    this->error(span, "unterminated text literal; a literal ends on the line it "
                                      "starts on");
                }

                this->emit(TokenKind::TEXT, span, this->stream.add_text_literal(std::move(literal)));
            }

            void scan_escape(std::string& chunk)
            {
                const std::size_t start = this->position;
                this->advance();

                if (this->at_end() || this->peek() == '\n')
                {
                    this->error(this->span_from(start), "a text literal cannot end with a backslash");
                    return;
                }

                const std::string_view escaped = this->current_code_point();
                this->position += escaped.size();

                if (escaped.size() == 1)
                {
                    switch (escaped.front())
                    {
                        case '"':  chunk.push_back('"');  return;
                        case '\\': chunk.push_back('\\'); return;
                        case 'n':  chunk.push_back('\n'); return;

                        // Exists for the sake of "\/{", the only way to write the
                        // interpolation marker as ordinary text.
                        case '/':  chunk.push_back('/');  return;

                        default: break;
                    }
                }

                this->error(this->span_from(start), "unknown escape sequence '\\{}'", escaped);

                // Recovered as the character itself: whatever the author meant, a
                // dropped character would be a second, silent mistake.
                chunk += escaped;
            }

            void scan_interpolation(TextLiteral& literal)
            {
                const std::size_t marker = this->position;
                this->advance();
                this->advance();

                const std::size_t name_start = this->position;
                if (!is_identifier_start(this->peek()))
                {
                    this->error(this->span_from(marker),
                                "expected a variable name after '/{{'");
                    return;
                }

                while (is_identifier_continuation(this->peek()))
                {
                    this->advance();
                }

                const SourceSpan name_span = this->span_from(name_start);
                std::string name(this->text.substr(name_start, name_span.length));

                if (this->peek() != '}')
                {
                    this->error(name_span,
                                "expected '}}' to close the interpolation of '{}'", name);
                    return;
                }

                this->advance();
                literal.parts.emplace_back(TextInterpolation{
                    .variable = std::move(name),
                    .span = name_span,
                });
            }

            TokenStream stream;
            std::string_view text;
            std::vector<Diagnostic> diagnostics;

            std::size_t position = 0;

            /// Widths of the blocks currently open, outermost first. The zero at
            /// the bottom is the file itself and is never popped.
            std::vector<std::uint32_t> indent_stack{0};

            char indent_character = '\0';
            SourceSpan indent_origin{};
            bool reported_indentation_mismatch = false;
        };
    }

    TokenizeResult tokenize(std::string source_name, std::string source)
    {
        return Lexer(std::move(source_name), std::move(source)).run();
    }
}
