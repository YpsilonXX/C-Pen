#include "cpen/script/parser.hh"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cpen::script
{
    namespace
    {
        class Parser
        {
        public:
            explicit Parser(const TokenStream& tokens)
                : stream(tokens)
            {
            }

            ParseResult run()
            {
                while (!this->at_end())
                {
                    // Nothing at the outermost level may be indented, and the lexer
                    // has already paired every INDENT with a DEDENT, so one of these
                    // here means the file itself begins in the middle of a block.
                    if (this->check(TokenKind::INDENT))
                    {
                        this->error_at(this->peek().span,
                                       "unexpected indentation: this line is inside no block");
                        this->advance();
                        continue;
                    }

                    // The closer of an indent already reported above. Saying it a
                    // second time would double every such report.
                    if (this->check(TokenKind::DEDENT))
                    {
                        this->advance();
                        continue;
                    }

                    if (this->check(TokenKind::NEWLINE))
                    {
                        this->advance();
                        continue;
                    }

                    if (const std::optional<StatementId> statement = this->parse_statement())
                    {
                        this->tree.add_root(*statement);
                    }
                    else
                    {
                        this->synchronise();
                    }
                }

                return ParseResult{
                    .tree = std::move(this->tree),
                    .diagnostics = std::move(this->diagnostics),
                };
            }

        private:
            using StatementParser = std::optional<StatementId> (Parser::*)();

            struct StatementEntry
            {
                /// The token that can only begin this statement.
                TokenKind leading;
                StatementParser parse;
            };

            const Token& peek(const std::size_t lookahead = 0) const noexcept
            {
                const std::vector<Token>& tokens = this->stream.tokens();
                const std::size_t index = this->current + lookahead;

                // The stream always ends with END_OF_FILE, so clamping means every
                // lookahead has something to return and no caller needs a bounds
                // check of its own.
                return tokens[index < tokens.size() ? index : tokens.size() - 1];
            }

            TokenKind peek_kind(const std::size_t lookahead = 0) const noexcept
            {
                return this->peek(lookahead).kind;
            }

            const Token& previous() const noexcept
            {
                return this->stream.tokens()[this->current > 0 ? this->current - 1 : 0];
            }

            bool at_end() const noexcept { return this->peek_kind() == TokenKind::END_OF_FILE; }

            bool check(const TokenKind kind) const noexcept { return this->peek_kind() == kind; }

            const Token& advance() noexcept
            {
                const Token& token = this->peek();
                if (this->current + 1 < this->stream.tokens().size())
                {
                    ++this->current;
                }

                return token;
            }

            bool match(const TokenKind kind) noexcept
            {
                if (!this->check(kind))
                {
                    return false;
                }

                this->advance();
                return true;
            }

            template <typename... Arguments>
            void error_at(const SourceSpan span,
                          const std::format_string<Arguments...> format,
                          Arguments&&... arguments)
            {
                this->diagnostics.push_back(make_diagnostic(Severity::ERROR, span, format,
                                                            std::forward<Arguments>(arguments)...));
            }

            /// Consumes the expected token, or reports what was found instead.
            ///
            /// `context` completes the sentence "expected X ..." — "after the label
            /// name", "before the choices" — because a message that only says what
            /// was expected leaves the author looking for which of the four colons
            /// on screen it meant.
            const Token* expect(const TokenKind kind, const std::string_view context)
            {
                if (this->check(kind))
                {
                    return &this->advance();
                }

                this->error_at(this->peek().span, "expected {} {}, but found {}",
                               describe(kind), context, describe(this->peek_kind()));
                return nullptr;
            }

            std::optional<std::string> expect_name(const std::string_view what)
            {
                if (this->check(TokenKind::IDENTIFIER))
                {
                    const Token& token = this->advance();
                    return std::string(this->stream.lexeme(token));
                }

                // A keyword here is the likeliest single mistake, and saying so is
                // worth more than repeating that an identifier was expected.
                const std::string_view found = describe(this->peek_kind());
                if (is_keyword(this->peek_kind()))
                {
                    this->error_at(this->peek().span,
                                   "expected {}, but {} is a keyword and cannot be used as a name",
                                   what, found);
                }
                else
                {
                    this->error_at(this->peek().span, "expected {}, but found {}", what, found);
                }

                return std::nullopt;
            }

            /// Consumes the end of a one-line statement.
            bool end_of_statement(const std::string_view construct)
            {
                if (this->match(TokenKind::NEWLINE))
                {
                    return true;
                }

                this->error_at(this->peek().span,
                               "expected the end of the line after {}, but found {}",
                               construct, describe(this->peek_kind()));
                return false;
            }

            /// Skips whatever is left of a statement that could not be parsed,
            /// including the block it opened.
            ///
            /// Skipping the block matters more than it looks: without it a mistyped
            /// `if` would leave its body to be parsed at the outer level, where
            /// perfectly good statements would be reported as wrongly indented and
            /// the one real mistake would be lost among them.
            void synchronise()
            {
                while (!this->at_end() && !this->check(TokenKind::NEWLINE))
                {
                    this->advance();
                }

                this->match(TokenKind::NEWLINE);

                if (!this->check(TokenKind::INDENT))
                {
                    return;
                }

                std::size_t depth = 0;
                do
                {
                    if (this->check(TokenKind::INDENT))
                    {
                        ++depth;
                    }
                    else if (this->check(TokenKind::DEDENT))
                    {
                        --depth;
                    }

                    this->advance();
                }
                while (depth > 0 && !this->at_end());
            }

            SourceSpan span_since(const SourceSpan start) const noexcept
            {
                return merge(start, this->previous().span);
            }

            // --- statements ----------------------------------------------------

            std::optional<StatementId> parse_statement()
            {
                // The dispatch table, and the reason a new statement costs one row
                // and one function rather than another branch in a chain that every
                // statement pays to walk past.
                static constexpr std::array<StatementEntry, 9> TABLE = {{
                    {TokenKind::LABEL, &Parser::parse_label},
                    {TokenKind::IF, &Parser::parse_if},
                    {TokenKind::MENU, &Parser::parse_menu},
                    {TokenKind::DOLLAR, &Parser::parse_assignment},
                    {TokenKind::SCENE, &Parser::parse_scene},
                    {TokenKind::SHOW, &Parser::parse_show},
                    {TokenKind::HIDE, &Parser::parse_hide},
                    {TokenKind::JUMP, &Parser::parse_jump},
                    {TokenKind::PAUSE, &Parser::parse_pause},
                }};

                const TokenKind kind = this->peek_kind();

                for (const StatementEntry& entry : TABLE)
                {
                    if (entry.leading == kind)
                    {
                        return (this->*entry.parse)();
                    }
                }

                // Dialogue is the one statement with no leading keyword: it is by
                // far the most common line in a script, and making it carry a word
                // would tax every line of the novel to spare the parser one lookahead.
                if (kind == TokenKind::TEXT
                    || (kind == TokenKind::IDENTIFIER && this->peek_kind(1) == TokenKind::TEXT))
                {
                    return this->parse_say();
                }

                if (kind == TokenKind::IDENTIFIER)
                {
                    this->error_at(this->peek().span,
                                   "'{}' begins no statement; a line that starts with a name is a "
                                   "character speaking and must continue with a text literal",
                                   this->stream.lexeme(this->peek()));
                }
                else
                {
                    this->error_at(this->peek().span, "{} begins no statement", describe(kind));
                }

                return std::nullopt;
            }

            /// Parses ':' NEWLINE INDENT statements DEDENT into `body`.
            bool parse_block(std::vector<StatementId>& body, const std::string_view construct)
            {
                if (this->expect(TokenKind::COLON, std::format("after the {}", construct)) == nullptr)
                {
                    return false;
                }

                if (!this->end_of_statement("':'"))
                {
                    return false;
                }

                if (!this->match(TokenKind::INDENT))
                {
                    this->error_at(this->peek().span,
                                   "expected an indented block after the {}", construct);
                    return false;
                }

                while (!this->check(TokenKind::DEDENT) && !this->at_end())
                {
                    if (const std::optional<StatementId> statement = this->parse_statement())
                    {
                        body.push_back(*statement);
                    }
                    else
                    {
                        this->synchronise();
                    }
                }

                this->match(TokenKind::DEDENT);
                return true;
            }

            std::optional<StatementId> parse_label()
            {
                const SourceSpan start = this->advance().span;

                const std::optional<std::string> name = this->expect_name("a label name");
                if (!name.has_value())
                {
                    return std::nullopt;
                }

                LabelStatement node{.name = *name};
                if (!this->parse_block(node.body, "label name"))
                {
                    return std::nullopt;
                }

                node.span = this->span_since(start);
                return this->tree.add(std::move(node));
            }

            std::optional<StatementId> parse_if()
            {
                const SourceSpan start = this->advance().span;

                IfStatement node;
                if (!this->parse_branch(node.branches, "condition"))
                {
                    return std::nullopt;
                }

                while (this->match(TokenKind::ELIF))
                {
                    if (!this->parse_branch(node.branches, "condition"))
                    {
                        return std::nullopt;
                    }
                }

                if (this->match(TokenKind::ELSE))
                {
                    if (!this->parse_block(node.otherwise, "'else'"))
                    {
                        return std::nullopt;
                    }
                }

                node.span = this->span_since(start);
                return this->tree.add(std::move(node));
            }

            bool parse_branch(std::vector<ConditionalBranch>& branches, const std::string_view construct)
            {
                const SourceSpan start = this->peek().span;

                const std::optional<ExpressionId> condition = this->parse_expression();
                if (!condition.has_value())
                {
                    return false;
                }

                ConditionalBranch branch{.condition = *condition};
                if (!this->parse_block(branch.body, construct))
                {
                    return false;
                }

                branch.span = this->span_since(start);
                branches.push_back(std::move(branch));
                return true;
            }

            std::optional<StatementId> parse_menu()
            {
                const SourceSpan start = this->advance().span;

                if (this->expect(TokenKind::COLON, "after 'menu'") == nullptr
                    || !this->end_of_statement("':'"))
                {
                    return std::nullopt;
                }

                if (!this->match(TokenKind::INDENT))
                {
                    this->error_at(this->peek().span, "expected an indented list of choices after 'menu'");
                    return std::nullopt;
                }

                MenuStatement node;

                while (!this->check(TokenKind::DEDENT) && !this->at_end())
                {
                    if (!this->check(TokenKind::TEXT))
                    {
                        this->error_at(this->peek().span,
                                       "expected the text of a choice, but found {}",
                                       describe(this->peek_kind()));
                        this->synchronise();
                        continue;
                    }

                    const SourceSpan choice_start = this->peek().span;
                    MenuChoice choice{.prompt = this->make_text(this->advance())};

                    if (!this->parse_block(choice.body, "text of the choice"))
                    {
                        this->synchronise();
                        continue;
                    }

                    choice.span = this->span_since(choice_start);
                    node.choices.push_back(std::move(choice));
                }

                this->match(TokenKind::DEDENT);

                if (node.choices.empty())
                {
                    this->error_at(this->span_since(start), "a menu must offer at least one choice");
                    return std::nullopt;
                }

                node.span = this->span_since(start);
                return this->tree.add(std::move(node));
            }

            std::optional<StatementId> parse_say()
            {
                const SourceSpan start = this->peek().span;

                std::optional<std::string> speaker;
                if (this->check(TokenKind::IDENTIFIER))
                {
                    speaker = std::string(this->stream.lexeme(this->advance()));
                }

                const Token* text = this->expect(TokenKind::TEXT, "as the line that is spoken");
                if (text == nullptr)
                {
                    return std::nullopt;
                }

                SayStatement node{.speaker = std::move(speaker), .text = this->make_text(*text)};
                if (!this->end_of_statement("a line of dialogue"))
                {
                    return std::nullopt;
                }

                node.span = this->span_since(start);
                return this->tree.add(std::move(node));
            }

            std::optional<StatementId> parse_assignment()
            {
                const SourceSpan start = this->advance().span;

                const std::optional<std::string> name = this->expect_name("a variable name after '$'");
                if (!name.has_value() || this->expect(TokenKind::EQUAL, "after the variable name") == nullptr)
                {
                    return std::nullopt;
                }

                const std::optional<ExpressionId> value = this->parse_expression();
                if (!value.has_value())
                {
                    return std::nullopt;
                }

                AssignmentStatement node{.variable = *name, .value = *value};
                if (!this->end_of_statement("an assignment"))
                {
                    return std::nullopt;
                }

                node.span = this->span_since(start);
                return this->tree.add(std::move(node));
            }

            std::optional<StatementId> parse_scene()
            {
                const SourceSpan start = this->advance().span;

                const std::optional<std::string> background = this->expect_name("a background name");
                if (!background.has_value())
                {
                    return std::nullopt;
                }

                SceneStatement node{.background = *background, .transition = this->parse_transition()};
                if (!this->end_of_statement("'scene'"))
                {
                    return std::nullopt;
                }

                node.span = this->span_since(start);
                return this->tree.add(std::move(node));
            }

            std::optional<StatementId> parse_show()
            {
                const SourceSpan start = this->advance().span;

                const std::optional<std::string> name = this->expect_name("the name of what to show");
                if (!name.has_value())
                {
                    return std::nullopt;
                }

                ShowStatement node{.name = *name};

                // 'at' and 'with' are keywords with tokens of their own, so the run
                // of attributes ends at them without a word of lookahead.
                while (this->check(TokenKind::IDENTIFIER))
                {
                    node.attributes.emplace_back(this->stream.lexeme(this->advance()));
                }

                node.position = this->parse_position();
                node.transition = this->parse_transition();
                if (!this->end_of_statement("'show'"))
                {
                    return std::nullopt;
                }

                node.span = this->span_since(start);
                return this->tree.add(std::move(node));
            }

            std::optional<StatementId> parse_hide()
            {
                const SourceSpan start = this->advance().span;

                const std::optional<std::string> name = this->expect_name("the name of what to hide");
                if (!name.has_value())
                {
                    return std::nullopt;
                }

                HideStatement node{.name = *name, .transition = this->parse_transition()};
                if (!this->end_of_statement("'hide'"))
                {
                    return std::nullopt;
                }

                node.span = this->span_since(start);
                return this->tree.add(std::move(node));
            }

            std::optional<StatementId> parse_jump()
            {
                const SourceSpan start = this->advance().span;

                const std::optional<std::string> label = this->expect_name("the label to jump to");
                if (!label.has_value())
                {
                    return std::nullopt;
                }

                if (!this->end_of_statement("'jump'"))
                {
                    return std::nullopt;
                }

                return this->tree.add(JumpStatement{.label = *label, .span = this->span_since(start)});
            }

            std::optional<StatementId> parse_pause()
            {
                const SourceSpan start = this->advance().span;

                const std::optional<ExpressionId> duration = this->parse_expression();
                if (!duration.has_value())
                {
                    return std::nullopt;
                }

                if (!this->end_of_statement("'pause'"))
                {
                    return std::nullopt;
                }

                return this->tree.add(PauseStatement{
                    .duration = *duration,
                    .span = this->span_since(start),
                });
            }

            std::optional<Position> parse_position()
            {
                if (!this->check(TokenKind::AT))
                {
                    return std::nullopt;
                }

                const SourceSpan start = this->advance().span;

                if (!this->match(TokenKind::LEFT_PARENTHESIS))
                {
                    const std::optional<std::string> anchor = this->expect_name("an anchor name after 'at'");
                    if (!anchor.has_value())
                    {
                        return std::nullopt;
                    }

                    return AnchorPosition{.name = *anchor, .span = this->span_since(start)};
                }

                const std::optional<ExpressionId> x = this->parse_expression();
                if (!x.has_value() || this->expect(TokenKind::COMMA, "between the coordinates") == nullptr)
                {
                    return std::nullopt;
                }

                const std::optional<ExpressionId> y = this->parse_expression();
                if (!y.has_value()
                    || this->expect(TokenKind::RIGHT_PARENTHESIS, "after the coordinates") == nullptr)
                {
                    return std::nullopt;
                }

                return CoordinatePosition{.x = *x, .y = *y, .span = this->span_since(start)};
            }

            std::optional<Transition> parse_transition()
            {
                if (!this->check(TokenKind::WITH))
                {
                    return std::nullopt;
                }

                const SourceSpan start = this->advance().span;

                const std::optional<std::string> name = this->expect_name("a transition name after 'with'");
                if (!name.has_value())
                {
                    return std::nullopt;
                }

                return Transition{.name = *name, .span = this->span_since(start)};
            }

            // --- expressions ---------------------------------------------------

            /// The Pratt loop: parse one operand, then keep absorbing operators
            /// that bind at least as tightly as the caller allows.
            std::optional<ExpressionId> parse_expression(const Precedence minimum = Precedence::OR)
            {
                std::optional<ExpressionId> left = this->parse_operand();
                if (!left.has_value())
                {
                    return std::nullopt;
                }

                while (true)
                {
                    const TokenKind kind = this->peek_kind();
                    const Precedence precedence = infix_precedence(kind);

                    if (precedence < minimum)
                    {
                        break;
                    }

                    const SourceSpan start = span_of(this->tree.expression(*left));
                    this->advance();

                    // One level tighter on the right: every operator here associates
                    // to the left, so an operator of equal precedence must end this
                    // operand rather than join it.
                    const std::optional<ExpressionId> right = this->parse_expression(tighter(precedence));
                    if (!right.has_value())
                    {
                        return std::nullopt;
                    }

                    const SourceSpan span = merge(start, span_of(this->tree.expression(*right)));

                    if (const std::optional<BinaryOperator> binary = find_binary_operator(kind))
                    {
                        left = this->tree.add(BinaryExpression{
                            .operation = *binary,
                            .left = *left,
                            .right = *right,
                            .span = span,
                        });
                    }
                    else
                    {
                        left = this->tree.add(LogicalExpression{
                            .operation = *find_logical_operator(kind),
                            .left = *left,
                            .right = *right,
                            .span = span,
                        });
                    }
                }

                return left;
            }

            std::optional<ExpressionId> parse_operand()
            {
                const Token& token = this->peek();

                switch (token.kind)
                {
                    case TokenKind::INTEGER:
                        this->advance();
                        return this->make_integer(token);

                    case TokenKind::FLOATING:
                        this->advance();
                        return this->make_floating(token);

                    case TokenKind::TEXT:
                        this->advance();
                        return this->make_text(token);

                    case TokenKind::TRUE_LITERAL:
                    case TokenKind::FALSE_LITERAL:
                        this->advance();
                        return this->tree.add(LiteralExpression{
                            .value = core::Value(token.kind == TokenKind::TRUE_LITERAL),
                            .span = token.span,
                        });

                    case TokenKind::NIL_LITERAL:
                        this->advance();
                        return this->tree.add(LiteralExpression{.value = core::Value{}, .span = token.span});

                    case TokenKind::IDENTIFIER:
                        this->advance();
                        return this->tree.add(VariableExpression{
                            .name = std::string(this->stream.lexeme(token)),
                            .span = token.span,
                        });

                    case TokenKind::LEFT_PARENTHESIS:
                    {
                        this->advance();
                        const std::optional<ExpressionId> inner = this->parse_expression();
                        if (!inner.has_value()
                            || this->expect(TokenKind::RIGHT_PARENTHESIS, "after the expression") == nullptr)
                        {
                            return std::nullopt;
                        }

                        return inner;
                    }

                    default:
                        break;
                }

                if (const std::optional<UnaryOperator> unary = find_unary_operator(token.kind))
                {
                    this->advance();

                    const std::optional<ExpressionId> operand = this->parse_expression(Precedence::UNARY);
                    if (!operand.has_value())
                    {
                        return std::nullopt;
                    }

                    return this->tree.add(UnaryExpression{
                        .operation = *unary,
                        .operand = *operand,
                        .span = merge(token.span, span_of(this->tree.expression(*operand))),
                    });
                }

                this->error_at(token.span, "expected a value, but found {}", describe(token.kind));
                return std::nullopt;
            }

            ExpressionId make_text(const Token& token)
            {
                const TextLiteral* literal = this->stream.text_literal(token);

                return this->tree.add(TextExpression{
                    .parts = literal != nullptr ? literal->parts : std::vector<TextPart>{},
                    .span = token.span,
                });
            }

            ExpressionId make_integer(const Token& token)
            {
                const std::string_view lexeme = this->stream.lexeme(token);

                std::int64_t value = 0;
                const std::from_chars_result parsed =
                    std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value);

                // The lexer guarantees the digits; only the range can fail here,
                // which is exactly why the conversion lives at this layer and not
                // in the lexer — how wide an integer is is a property of the value
                // model, not of the text.
                if (parsed.ec != std::errc{})
                {
                    this->error_at(token.span,
                                   "the number {} is too large for an integer", lexeme);
                }

                return this->tree.add(LiteralExpression{.value = core::Value(value), .span = token.span});
            }

            ExpressionId make_floating(const Token& token)
            {
                const std::string_view lexeme = this->stream.lexeme(token);

                double value = 0.0;
                const std::from_chars_result parsed =
                    std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value);

                if (parsed.ec != std::errc{})
                {
                    this->error_at(token.span, "the number {} cannot be represented", lexeme);
                }

                return this->tree.add(LiteralExpression{.value = core::Value(value), .span = token.span});
            }

            const TokenStream& stream;
            std::size_t current = 0;
            SyntaxTree tree;
            std::vector<Diagnostic> diagnostics;
        };
    }

    ParseResult parse(const TokenStream& stream)
    {
        return Parser(stream).run();
    }
}
