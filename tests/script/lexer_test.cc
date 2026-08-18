#include <catch2/catch_test_macros.hpp>

#include "cpen/script/diagnostic.hh"
#include "cpen/script/lexer.hh"
#include "cpen/script/token.hh"
#include "support/trace.hh"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

using cpen::script::Diagnostic;
using cpen::script::dump_tokens;
using cpen::script::render_diagnostic;
using cpen::script::TextChunk;
using cpen::script::TextInterpolation;
using cpen::script::TextLiteral;
using cpen::script::TextPart;
using cpen::script::Token;
using cpen::script::tokenize;
using cpen::script::TokenizeResult;
using cpen::script::TokenKind;
using cpen::script::TokenStream;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    TokenizeResult lex(std::string source)
    {
        return tokenize("test.pen", std::move(source));
    }

    std::vector<TokenKind> kinds_of(const TokenStream& stream)
    {
        std::vector<TokenKind> kinds;
        for (const Token& token : stream.tokens())
        {
            kinds.push_back(token.kind);
        }

        return kinds;
    }

    /// Squeezes runs of spaces out of a token dump.
    ///
    /// The dump pads its columns so that a person can read it; a test that
    /// compared the padding too would fail whenever a token kind grew longer than
    /// the widest one, which says nothing about the lexer. What is pinned here is
    /// the content of each line — position, kind, lexeme — in order.
    std::string compact(const std::string& dumped)
    {
        std::string result;
        bool at_line_start = true;
        bool pending_space = false;

        for (const char character : dumped)
        {
            if (character == '\n')
            {
                result.push_back('\n');
                at_line_start = true;
                pending_space = false;
                continue;
            }

            if (character == ' ')
            {
                pending_space = !at_line_start;
                continue;
            }

            if (pending_space)
            {
                result.push_back(' ');
                pending_space = false;
            }

            result.push_back(character);
            at_line_start = false;
        }

        return result;
    }

    const Token& first_of(const TokenStream& stream, const TokenKind kind)
    {
        for (const Token& token : stream.tokens())
        {
            if (token.kind == kind)
            {
                return token;
            }
        }

        FAIL("no token of the requested kind was produced");
        return stream.tokens().front();
    }

    /// Renders a text literal's parts as "[chunk]{variable}[chunk]", which is
    /// short enough to state in a REQUIRE and shows the split, not just the text.
    std::string parts_of(const TokenStream& stream, const Token& token)
    {
        const TextLiteral* literal = stream.text_literal(token);
        REQUIRE(literal != nullptr);

        std::string rendered;
        for (const TextPart& part : literal->parts)
        {
            if (const auto* chunk = std::get_if<TextChunk>(&part))
            {
                rendered += "[" + chunk->text + "]";
            }
            else
            {
                rendered += "{" + std::get<TextInterpolation>(part).variable + "}";
            }
        }

        return rendered;
    }

    /// The message of the one diagnostic a case expected to provoke.
    std::string only_message(const TokenizeResult& result)
    {
        REQUIRE(result.diagnostics.size() == 1);
        trace("reported: {}", result.diagnostics.front().message);

        return result.diagnostics.front().message;
    }

    bool contains(const std::string_view haystack, const std::string_view needle)
    {
        return haystack.find(needle) != std::string_view::npos;
    }
}

TEST_CASE("a script tokenises to the expected dump", "[script][lexer]")
{
    // The golden case. Every rule below is also tested on its own, but this is the
    // one that shows what changing a lexical rule costs: a change that was meant
    // to affect one construct and quietly affects another shows up here as a diff
    // over a whole script.
    const TokenizeResult result = lex("label start:\n\talice \"Привет!\"\n");

    REQUIRE(result.diagnostics.empty());

    constexpr std::string_view expected =
        "1:1 LABEL 'label'\n"
        "1:7 IDENTIFIER 'start'\n"
        "1:12 COLON ':'\n"
        "1:13 NEWLINE\n"
        "2:2 INDENT\n"
        "2:2 IDENTIFIER 'alice'\n"
        "2:8 TEXT \"Привет!\"\n"
        "2:17 NEWLINE\n"
        "3:1 DEDENT\n"
        "3:1 END_OF_FILE\n";

    const std::string actual = compact(dump_tokens(result.stream));

    trace_step("the dump as the lexer produced it");
    trace("\n{}", dump_tokens(result.stream));

    REQUIRE(actual == expected);
}

TEST_CASE("blocks open and close with the indentation", "[script][lexer]")
{
    const TokenizeResult result = lex("if a:\n"
                                      "    if b:\n"
                                      "        pause 1\n"
                                      "\"end\"\n");

    REQUIRE(result.diagnostics.empty());

    const std::vector<TokenKind> expected = {
        TokenKind::IF, TokenKind::IDENTIFIER, TokenKind::COLON, TokenKind::NEWLINE,
        TokenKind::INDENT,
        TokenKind::IF, TokenKind::IDENTIFIER, TokenKind::COLON, TokenKind::NEWLINE,
        TokenKind::INDENT,
        TokenKind::PAUSE, TokenKind::INTEGER, TokenKind::NEWLINE,
        // Both blocks close before the line that closed them, not after it.
        TokenKind::DEDENT, TokenKind::DEDENT,
        TokenKind::TEXT, TokenKind::NEWLINE,
        TokenKind::END_OF_FILE,
    };

    REQUIRE(kinds_of(result.stream) == expected);
}

TEST_CASE("a block left open at the end of the file is closed anyway", "[script][lexer]")
{
    const TokenizeResult result = lex("if a:\n\tpause 1\n");

    REQUIRE(result.diagnostics.empty());

    const std::vector<TokenKind> kinds = kinds_of(result.stream);
    REQUIRE(kinds.size() >= 2);

    // The parser is never handed a block that the end of input closed implicitly:
    // that is one fewer special case in every statement it knows.
    REQUIRE(kinds[kinds.size() - 2] == TokenKind::DEDENT);
    REQUIRE(kinds.back() == TokenKind::END_OF_FILE);
}

TEST_CASE("a file may be indented with tabs or with spaces", "[script][lexer]")
{
    trace_step("tabs");
    REQUIRE(lex("if a:\n\tpause 1\n").diagnostics.empty());

    trace_step("spaces");
    REQUIRE(lex("if a:\n    pause 1\n").diagnostics.empty());
}

TEST_CASE("a file may not be indented with both", "[script][lexer]")
{
    trace_step("a space-indented line in a tab-indented file");
    {
        const TokenizeResult result = lex("if a:\n\tpause 1\n"
                                          "if b:\n    pause 2\n");

        const std::string message = only_message(result);
        REQUIRE(contains(message, "indented with spaces"));
        REQUIRE(contains(message, "indented with tabs"));

        // The line the file's choice was made on, so the author can see both ends
        // of the disagreement without hunting for the first indent themselves.
        REQUIRE(contains(message, "line 2"));
    }

    trace_step("one line that mixes the two");
    {
        const TokenizeResult result = lex("if a:\n\tpause 1\n"
                                          "if b:\n\t pause 2\n");

        REQUIRE(contains(only_message(result), "mixes tabs and spaces"));
    }

    trace_step("reported once, however many lines are wrong");
    {
        const TokenizeResult result = lex("if a:\n\tpause 1\n"
                                          "if b:\n    pause 2\n"
                                          "if c:\n    pause 3\n"
                                          "if d:\n    pause 4\n");

        // A file converted wholesale by an editor is one mistake, not forty; the
        // rest of the run has to stay readable.
        REQUIRE(result.diagnostics.size() == 1);
    }
}

TEST_CASE("blank and comment lines do not close a block", "[script][lexer]")
{
    // The comment is indented with spaces inside a tab-indented file on purpose:
    // whitespace on a line that carries nothing is invisible, and holding it to
    // the homogeneity rule would fail scripts that read as correct.
    const TokenizeResult result = lex("if a:\n"
                                      "\tpause 1\n"
                                      "\n"
                                      "    # a comment\n"
                                      "\n"
                                      "\tpause 2\n");

    REQUIRE(result.diagnostics.empty());

    const std::vector<TokenKind> expected = {
        TokenKind::IF, TokenKind::IDENTIFIER, TokenKind::COLON, TokenKind::NEWLINE,
        TokenKind::INDENT,
        TokenKind::PAUSE, TokenKind::INTEGER, TokenKind::NEWLINE,
        TokenKind::PAUSE, TokenKind::INTEGER, TokenKind::NEWLINE,
        TokenKind::DEDENT,
        TokenKind::END_OF_FILE,
    };

    REQUIRE(kinds_of(result.stream) == expected);
}

TEST_CASE("a dedent to a width no block has is reported", "[script][lexer]")
{
    const TokenizeResult result = lex("if a:\n"
                                      "        pause 1\n"
                                      "    pause 2\n");

    REQUIRE(contains(only_message(result), "no enclosing block"));

    // Recovered by adopting the width, so the file still ends with its blocks in
    // balance and the parser sees one problem rather than a cascade.
    REQUIRE(kinds_of(result.stream).back() == TokenKind::END_OF_FILE);
}

TEST_CASE("a comment runs to the end of its line", "[script][lexer]")
{
    const TokenizeResult result = lex("pause 1 # waits a beat\npause 2\n");

    REQUIRE(result.diagnostics.empty());

    const std::vector<TokenKind> expected = {
        TokenKind::PAUSE, TokenKind::INTEGER, TokenKind::NEWLINE,
        TokenKind::PAUSE, TokenKind::INTEGER, TokenKind::NEWLINE,
        TokenKind::END_OF_FILE,
    };

    REQUIRE(kinds_of(result.stream) == expected);
}

TEST_CASE("a keyword is only a keyword when it is the whole word", "[script][lexer]")
{
    const TokenizeResult result = lex("label labels лейбл _x1\n");

    REQUIRE(result.diagnostics.empty());

    const std::vector<TokenKind> expected = {
        TokenKind::LABEL,
        TokenKind::IDENTIFIER,
        // Cyrillic identifiers work by treating every byte at or above 0x80 as a
        // name character, which is the whole of the language's Unicode support
        // here — and the reason `$ симпатия = 0` needs no tables to lex.
        TokenKind::IDENTIFIER,
        TokenKind::IDENTIFIER,
        TokenKind::NEWLINE,
        TokenKind::END_OF_FILE,
    };

    REQUIRE(kinds_of(result.stream) == expected);
}

TEST_CASE("numbers are delimited but not converted", "[script][lexer]")
{
    trace_step("integers and fractions");
    {
        const TokenizeResult result = lex("0 42 2.5\n");

        REQUIRE(result.diagnostics.empty());

        const std::vector<TokenKind> expected = {
            TokenKind::INTEGER, TokenKind::INTEGER, TokenKind::FLOATING,
            TokenKind::NEWLINE, TokenKind::END_OF_FILE,
        };

        REQUIRE(kinds_of(result.stream) == expected);
        REQUIRE(result.stream.lexeme(result.stream.tokens()[2]) == "2.5");
    }

    trace_step("a decimal point with nothing after it");
    {
        const TokenizeResult result = lex("pause 3.\n");
        REQUIRE(contains(only_message(result), "followed by a digit"));
    }
}

TEST_CASE("operators are scanned longest first", "[script][lexer]")
{
    const TokenizeResult result = lex("== != <= >= < > = + - * / % $ : , ( )\n");

    REQUIRE(result.diagnostics.empty());

    const std::vector<TokenKind> expected = {
        TokenKind::EQUAL_EQUAL, TokenKind::NOT_EQUAL,
        TokenKind::LESS_EQUAL, TokenKind::GREATER_EQUAL,
        TokenKind::LESS, TokenKind::GREATER, TokenKind::EQUAL,
        TokenKind::PLUS, TokenKind::MINUS, TokenKind::STAR, TokenKind::SLASH,
        TokenKind::PERCENT, TokenKind::DOLLAR, TokenKind::COLON, TokenKind::COMMA,
        TokenKind::LEFT_PARENTHESIS, TokenKind::RIGHT_PARENTHESIS,
        TokenKind::NEWLINE, TokenKind::END_OF_FILE,
    };

    REQUIRE(kinds_of(result.stream) == expected);
}

TEST_CASE("a lone exclamation mark is not negation", "[script][lexer]")
{
    // Negation is a word in this language. Saying so is worth more than reporting
    // an unexpected character, because '!' is what the author's other languages
    // taught them to write.
    REQUIRE(contains(only_message(lex("$ x = !y\n")), "written 'not'"));
}

TEST_CASE("an unexpected character is reported whole", "[script][lexer]")
{
    const TokenizeResult result = lex("$ x = @\n");

    REQUIRE(contains(only_message(result), "'@'"));

    // Skipped rather than swallowing the line, so what follows is still lexed.
    REQUIRE(kinds_of(result.stream).back() == TokenKind::END_OF_FILE);
}

TEST_CASE("escape sequences in a text literal", "[script][lexer]")
{
    trace_step("the four the language defines");
    {
        const TokenizeResult result = lex("\"a\\\"b\\\\c\\nd\\/e\"\n");

        REQUIRE(result.diagnostics.empty());
        REQUIRE(parts_of(result.stream, first_of(result.stream, TokenKind::TEXT))
                == "[a\"b\\c\nd/e]");
    }

    trace_step("an unknown one is reported and taken literally");
    {
        const TokenizeResult result = lex("\"a\\qb\"\n");

        REQUIRE(contains(only_message(result), "unknown escape sequence"));

        // Dropping the character would be a second mistake, and a silent one.
        REQUIRE(parts_of(result.stream, first_of(result.stream, TokenKind::TEXT)) == "[aqb]");
    }
}

TEST_CASE("interpolation splits a text literal into parts", "[script][lexer]")
{
    const TokenizeResult result = lex("\"У тебя /{sympathy} очков.\"\n");

    REQUIRE(result.diagnostics.empty());
    REQUIRE(parts_of(result.stream, first_of(result.stream, TokenKind::TEXT))
            == "[У тебя ]{sympathy}[ очков.]");
}

TEST_CASE("a brace on its own is ordinary text", "[script][lexer]")
{
    // The reason the marker is "/{" and not "{": prose is full of punctuation, and
    // an author who writes a brace should not have to know that the language
    // wanted it escaped.
    const TokenizeResult result = lex("\"{x} } {\"\n");

    REQUIRE(result.diagnostics.empty());
    REQUIRE(parts_of(result.stream, first_of(result.stream, TokenKind::TEXT)) == "[{x} } {]");
}

TEST_CASE("a literal interpolation marker is written with a backslash", "[script][lexer]")
{
    const TokenizeResult result = lex("\"\\/{x}\"\n");

    REQUIRE(result.diagnostics.empty());
    REQUIRE(parts_of(result.stream, first_of(result.stream, TokenKind::TEXT)) == "[/{x}]");
}

TEST_CASE("a malformed interpolation is reported", "[script][lexer]")
{
    trace_step("no name");
    REQUIRE(contains(only_message(lex("\"/{}\"\n")), "expected a variable name"));

    trace_step("no closing brace");
    REQUIRE(contains(only_message(lex("\"/{x\"\n")), "to close the interpolation"));
}

TEST_CASE("a text literal ends on the line it starts on", "[script][lexer]")
{
    const TokenizeResult result = lex("alice \"unfinished\nalice \"next\"\n");

    REQUIRE(contains(only_message(result), "unterminated"));

    // The token is still produced: the line after it is a line of script, and
    // refusing to lex it would turn one mistake into a file's worth.
    REQUIRE(kinds_of(result.stream).back() == TokenKind::END_OF_FILE);
}

TEST_CASE("an empty text literal is still a literal", "[script][lexer]")
{
    const TokenizeResult result = lex("\"\"\n");

    REQUIRE(result.diagnostics.empty());
    REQUIRE(parts_of(result.stream, first_of(result.stream, TokenKind::TEXT)) == "[]");
}

TEST_CASE("a byte-order mark and CRLF endings are normalised away", "[script][lexer]")
{
    const TokenizeResult result = lex("\xEF\xBB\xBF" "label start:\r\n\tpause 1\r\n");

    REQUIRE(result.diagnostics.empty());

    const std::vector<TokenKind> expected = {
        TokenKind::LABEL, TokenKind::IDENTIFIER, TokenKind::COLON, TokenKind::NEWLINE,
        TokenKind::INDENT,
        TokenKind::PAUSE, TokenKind::INTEGER, TokenKind::NEWLINE,
        TokenKind::DEDENT,
        TokenKind::END_OF_FILE,
    };

    REQUIRE(kinds_of(result.stream) == expected);

    // The mark is gone rather than lexed as the start of a name, and the line it
    // sat on is still line one.
    REQUIRE(result.stream.lexeme(result.stream.tokens().front()) == "label");
}

TEST_CASE("a diagnostic points at the character it means", "[script][diagnostic]")
{
    const TokenizeResult result = lex("$ имя = @\n");
    REQUIRE(result.diagnostics.size() == 1);

    const std::string rendered = render_diagnostic(result.diagnostics.front(),
                                                   result.stream.source(),
                                                   result.stream.source_name());
    trace("\n{}", rendered);

    // Columns count code points, not bytes: three Cyrillic letters occupy six
    // bytes and three columns, and a position that said 12 would look precise and
    // point at nothing.
    REQUIRE(contains(rendered, "test.pen:1:9: error:"));
    REQUIRE(contains(rendered, "$ имя = @\n        ^"));
}
