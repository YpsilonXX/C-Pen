#include <catch2/catch_test_macros.hpp>

#include "cpen/script/lexer.hh"
#include "cpen/script/parser.hh"
#include "cpen/script/syntax_tree.hh"
#include "support/trace.hh"

#include <string>
#include <string_view>
#include <utility>

using cpen::script::dump_syntax_tree;
using cpen::script::parse;
using cpen::script::ParseResult;
using cpen::script::tokenize;
using cpen::script::TokenizeResult;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    /// A lexed and parsed script, kept together because the parser borrows the
    /// token stream while it runs.
    struct Parsed
    {
        TokenizeResult lexed;
        ParseResult result;

        std::string dumped() const { return dump_syntax_tree(this->result.tree); }

        /// Every diagnostic as one blob, so a case can look for the sentence it
        /// cares about without counting the ones around it.
        std::string messages() const
        {
            std::string joined;
            for (const cpen::script::Diagnostic& diagnostic : this->result.diagnostics)
            {
                joined += diagnostic.message;
                joined.push_back('\n');
            }

            return joined;
        }
    };

    Parsed parse_script(std::string source)
    {
        TokenizeResult lexed = tokenize("test.pen", std::move(source));

        // Every case here is lexically sound: a lexer diagnostic would mean the
        // case is testing the wrong thing.
        REQUIRE(lexed.diagnostics.empty());

        ParseResult result = parse(lexed.stream);
        trace("\n{}", dump_syntax_tree(result.tree));

        return Parsed{.lexed = std::move(lexed), .result = std::move(result)};
    }

    bool contains(const std::string_view haystack, const std::string_view needle)
    {
        return haystack.find(needle) != std::string_view::npos;
    }
}

TEST_CASE("a script parses into the expected tree", "[script][parser]")
{
    // The golden case, in the same spirit as the lexer's: the whole grammar in one
    // script, so that a change meant for one construct cannot quietly reshape
    // another without saying so in a diff.
    const Parsed parsed = parse_script(
        "label start:\n"
        "\tscene bg_room with fade\n"
        "\tshow alice happy at left\n"
        "\talice \"Привет, /{name}!\"\n"
        "\t$ sympathy = 1 + 2 * 3\n"
        "\tif sympathy > 3 and not shy:\n"
        "\t\tjump hallway\n"
        "\telse:\n"
        "\t\tpause 0.5\n"
        "\tmenu:\n"
        "\t\t\"Уйти\":\n"
        "\t\t\tjump way_out\n");

    REQUIRE(parsed.result.diagnostics.empty());

    constexpr std::string_view expected =
        "label 'start'\n"
        "  scene 'bg_room' with 'fade'\n"
        "  show 'alice' 'happy' at anchor 'left'\n"
        "  say speaker='alice'\n"
        "    text \"Привет, \" /{name} \"!\"\n"
        "  assign 'sympathy'\n"
        "    binary ADD\n"
        "      literal 1\n"
        "      binary MULTIPLY\n"
        "        literal 2\n"
        "        literal 3\n"
        "  if\n"
        "    branch\n"
        "      condition\n"
        "        logical AND\n"
        "          binary GREATER\n"
        "            variable 'sympathy'\n"
        "            literal 3\n"
        "          unary NOT\n"
        "            variable 'shy'\n"
        "      body\n"
        "        jump 'hallway'\n"
        "    else\n"
        "      pause\n"
        "        literal 0.5\n"
        "  menu\n"
        "    choice\n"
        "      prompt\n"
        "        text \"Уйти\"\n"
        "      body\n"
        "        jump 'way_out'\n";

    REQUIRE(parsed.dumped() == expected);
}

TEST_CASE("multiplication binds tighter than addition", "[script][parser]")
{
    const Parsed parsed = parse_script("$ x = 1 + 2 * 3\n");

    REQUIRE(parsed.result.diagnostics.empty());
    REQUIRE(parsed.dumped() ==
            "assign 'x'\n"
            "  binary ADD\n"
            "    literal 1\n"
            "    binary MULTIPLY\n"
            "      literal 2\n"
            "      literal 3\n");
}

TEST_CASE("operators of equal precedence group to the left", "[script][parser]")
{
    // 1 - 2 - 3 is (1 - 2) - 3, not 1 - (2 - 3), which are different numbers.
    const Parsed parsed = parse_script("$ x = 1 - 2 - 3\n");

    REQUIRE(parsed.dumped() ==
            "assign 'x'\n"
            "  binary SUBTRACT\n"
            "    binary SUBTRACT\n"
            "      literal 1\n"
            "      literal 2\n"
            "    literal 3\n");
}

TEST_CASE("parentheses override precedence", "[script][parser]")
{
    const Parsed parsed = parse_script("$ x = (1 + 2) * 3\n");

    REQUIRE(parsed.dumped() ==
            "assign 'x'\n"
            "  binary MULTIPLY\n"
            "    binary ADD\n"
            "      literal 1\n"
            "      literal 2\n"
            "    literal 3\n");
}

TEST_CASE("logical operators are their own kind of node", "[script][parser]")
{
    // Not a nicety: `and` and `or` compile to jumps around their second operand,
    // and a compiler that met them as ordinary binary nodes would evaluate both
    // sides before deciding.
    const Parsed parsed = parse_script("$ x = a == 1 or b\n");

    REQUIRE(parsed.dumped() ==
            "assign 'x'\n"
            "  logical OR\n"
            "    binary EQUAL\n"
            "      variable 'a'\n"
            "      literal 1\n"
            "    variable 'b'\n");
}

TEST_CASE("prefix operators bind tighter than any infix one", "[script][parser]")
{
    const Parsed parsed = parse_script("$ x = -a + b\n");

    REQUIRE(parsed.dumped() ==
            "assign 'x'\n"
            "  binary ADD\n"
            "    unary NEGATE\n"
            "      variable 'a'\n"
            "    variable 'b'\n");
}

TEST_CASE("literals of every kind", "[script][parser]")
{
    const Parsed parsed = parse_script("$ a = true\n$ b = false\n$ c = nil\n$ d = 2.5\n");

    REQUIRE(parsed.result.diagnostics.empty());
    REQUIRE(parsed.dumped() ==
            "assign 'a'\n"
            "  literal true\n"
            "assign 'b'\n"
            "  literal false\n"
            "assign 'c'\n"
            "  literal nil\n"
            "assign 'd'\n"
            "  literal 2.5\n");
}

TEST_CASE("narration has no speaker", "[script][parser]")
{
    const Parsed parsed = parse_script("\"Дверь скрипнула.\"\nalice \"Привет!\"\n");

    REQUIRE(parsed.result.diagnostics.empty());
    REQUIRE(parsed.dumped() ==
            "say\n"
            "  text \"Дверь скрипнула.\"\n"
            "say speaker='alice'\n"
            "  text \"Привет!\"\n");
}

TEST_CASE("a sprite may be placed by anchor or by coordinates", "[script][parser]")
{
    trace_step("by anchor");
    REQUIRE(parse_script("show alice happy at left with dissolve\n").dumped()
            == "show 'alice' 'happy' at anchor 'left' with 'dissolve'\n");

    trace_step("by coordinates");
    REQUIRE(parse_script("show alice at (0.5, 0.8)\n").dumped() ==
            "show 'alice' at coordinates\n"
            "  literal 0.5\n"
            "  literal 0.8\n");
}

TEST_CASE("a conditional keeps its branches in one node", "[script][parser]")
{
    const Parsed parsed = parse_script("if a:\n"
                                       "\tpause 1\n"
                                       "elif b:\n"
                                       "\tpause 2\n"
                                       "else:\n"
                                       "\tpause 3\n");

    REQUIRE(parsed.result.diagnostics.empty());
    REQUIRE(parsed.dumped() ==
            "if\n"
            "  branch\n"
            "    condition\n"
            "      variable 'a'\n"
            "    body\n"
            "      pause\n"
            "        literal 1\n"
            "  branch\n"
            "    condition\n"
            "      variable 'b'\n"
            "    body\n"
            "      pause\n"
            "        literal 2\n"
            "  else\n"
            "    pause\n"
            "      literal 3\n");
}

TEST_CASE("a scene can be called and returned from", "[script][parser]")
{
    // The structure this exists for: one scene entered from several places, each
    // continuing where it left off. With `jump` alone the shared scene would have
    // to end in a chain of tests over a variable naming whoever entered it, and
    // every new caller would have to edit that chain.
    const Parsed parsed = parse_script("label kitchen:\n"
                                       "\tcall shared\n"
                                       "\t\"Вернулись на кухню.\"\n"
                                       "label shared:\n"
                                       "\t\"Свет мигнул.\"\n"
                                       "\treturn\n");

    REQUIRE(parsed.result.diagnostics.empty());
    REQUIRE(parsed.dumped() ==
            "label 'kitchen'\n"
            "  call 'shared'\n"
            "  say\n"
            "    text \"Вернулись на кухню.\"\n"
            "label 'shared'\n"
            "  say\n"
            "    text \"Свет мигнул.\"\n"
            "  return\n");
}

TEST_CASE("return takes no target", "[script][parser]")
{
    // A call always comes back to its caller, and that invariant is what makes the
    // call stack meaningful in a saved game. Somewhere else to continue is the
    // caller's decision, after the return.
    REQUIRE(contains(parse_script("return elsewhere\n").messages(), "end of the line"));
}

TEST_CASE("a statement that cannot be parsed does not stop the file", "[script][parser]")
{
    const Parsed parsed = parse_script("label start:\n"
                                       "\tjump one\n"
                                       "\talice\n"
                                       "\tjump two\n");

    REQUIRE(parsed.result.diagnostics.size() == 1);
    REQUIRE(contains(parsed.messages(), "begins no statement"));

    // The statements around the bad one are still there: one mistake is one
    // report, not the end of the run.
    REQUIRE(parsed.dumped() ==
            "label 'start'\n"
            "  jump 'one'\n"
            "  jump 'two'\n");
}

TEST_CASE("a broken statement takes its block with it", "[script][parser]")
{
    // Without skipping the block, the body of the mistyped `if` would be parsed at
    // the outer level and every line of it reported as wrongly placed — three
    // complaints about statements that are perfectly correct, and the one real
    // mistake lost among them.
    const Parsed parsed = parse_script("label start:\n"
                                       "\tjump one\n"
                                       "if:\n"
                                       "\tjump inner\n"
                                       "jump two\n");

    REQUIRE(parsed.result.diagnostics.size() == 1);
    REQUIRE(parsed.dumped() ==
            "label 'start'\n"
            "  jump 'one'\n"
            "jump 'two'\n");
}

TEST_CASE("what the parser says when a statement is malformed", "[script][parser]")
{
    trace_step("a label with no block");
    REQUIRE(contains(parse_script("label start\n").messages(), "after the label name"));

    trace_step("a block that is not indented");
    REQUIRE(contains(parse_script("label start:\n\"hi\"\n").messages(), "indented block"));

    trace_step("a name where a statement was expected");
    REQUIRE(contains(parse_script("alice\n").messages(), "'alice' begins no statement"));

    trace_step("a keyword used as a name");
    REQUIRE(contains(parse_script("$ if = 1\n").messages(), "is a keyword"));

    trace_step("an expression with nothing in it");
    REQUIRE(contains(parse_script("$ x =\n").messages(), "expected a value"));

    trace_step("a menu with no choices in it");
    REQUIRE(contains(parse_script("menu:\n\tpause 1\n").messages(), "at least one choice"));

    trace_step("something left over at the end of a line");
    {
        // Reported once and the line abandoned. Leaving the leftovers for the next
        // statement to meet would turn one mistake into two reports.
        const Parsed parsed = parse_script("alice \"hi\" oops\n");
        REQUIRE(parsed.result.diagnostics.size() == 1);
        REQUIRE(contains(parsed.messages(), "end of the line"));
    }

    trace_step("an unclosed parenthesis");
    REQUIRE(contains(parse_script("$ x = (1 + 2\n").messages(), "after the expression"));
}

TEST_CASE("a number too large for the value model is reported", "[script][parser]")
{
    // The lexer accepted these digits without a word, because how wide an integer
    // is is a question about the value model and not about the text.
    const Parsed parsed = parse_script("$ x = 99999999999999999999\n");

    REQUIRE(contains(parsed.messages(), "too large for an integer"));
}

TEST_CASE("indentation outside any block is reported once", "[script][parser]")
{
    const Parsed parsed = parse_script("\tjump one\n");

    REQUIRE(parsed.result.diagnostics.size() == 1);
    REQUIRE(contains(parsed.messages(), "unexpected indentation"));
}
