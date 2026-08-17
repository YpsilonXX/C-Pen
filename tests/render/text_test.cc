#include <catch2/catch_test_macros.hpp>

#include "cpen/render/text.hh"
#include "support/trace.hh"

#include <string>
#include <string_view>
#include <vector>

using cpen::render::wrap_text;
using cpen::test::trace;
using cpen::test::trace_step;

// Line breaking takes a function giving each code point's width rather than a
// Font, which is what puts these cases in the suite that runs without a typeface,
// a GL context or a driver. Every character below is ten wide, so a width in a case
// reads as a number of characters and a wrong break is obvious rather than
// arithmetic.

namespace
{
    constexpr float CHARACTER_WIDTH = 10.0f;

    float uniform_advance(char32_t)
    {
        return CHARACTER_WIDTH;
    }

    /// Widths for a proportional face, so that at least one case cannot be passed
    /// by counting characters: 'i' is narrow and 'W' is wide.
    float proportional_advance(const char32_t code_point)
    {
        if (code_point == U'i')
        {
            return 4.0f;
        }
        if (code_point == U'W')
        {
            return 20.0f;
        }
        return CHARACTER_WIDTH;
    }

    std::vector<std::string> wrap(const std::string_view text, const float width)
    {
        const std::vector<std::string_view> lines = wrap_text(text, width, uniform_advance);

        std::vector<std::string> owned;
        for (const std::string_view line : lines)
        {
            owned.emplace_back(line);
            trace("| {}", line);
        }
        return owned;
    }
}

TEST_CASE("text that fits stays on one line", "[render][text]")
{
    const std::vector<std::string> lines = wrap("abc", 100.0f);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "abc");
}

TEST_CASE("an empty string wraps to nothing at all", "[render][text]")
{
    const std::vector<std::string> lines = wrap("", 100.0f);
    CHECK(lines.empty());
}

TEST_CASE("a line breaks at a space", "[render][text]")
{
    // "abc def" is seventy wide; fifty fits five characters, so the break falls
    // inside "def" and has to be moved back to the space.
    const std::vector<std::string> lines = wrap("abc def", 50.0f);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "abc");
    CHECK(lines[1] == "def");
}

TEST_CASE("a run of spaces at a break belongs to neither line", "[render][text]")
{
    trace_step("three spaces between the words, none of them on either line");

    // The bug this pins down: recording the *latest* space as the break point
    // leaves every space but one hanging off the end of the line above, where they
    // are invisible until something measures the line or centres it.
    const std::vector<std::string> lines = wrap("abc   def", 50.0f);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "abc");
    CHECK(lines[1] == "def");
}

TEST_CASE("a word wider than the line is broken where it reaches the edge",
          "[render][text]")
{
    // Refusing to break it would put the line past the edge of the box, and no
    // amount of rewrapping at a larger width would ever bring it back.
    const std::vector<std::string> lines = wrap("abcdefgh", 50.0f);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "abcde");
    CHECK(lines[1] == "fgh");
}

TEST_CASE("a newline breaks whatever the width is", "[render][text]")
{
    SECTION("with room to spare")
    {
        const std::vector<std::string> lines = wrap("ab\ncd", 1000.0f);

        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == "ab");
        CHECK(lines[1] == "cd");
    }

    SECTION("and an empty line stays an empty line")
    {
        const std::vector<std::string> lines = wrap("ab\n\ncd", 1000.0f);

        REQUIRE(lines.size() == 3);
        CHECK(lines[0] == "ab");
        CHECK(lines[1].empty());
        CHECK(lines[2] == "cd");
    }

    SECTION("and a carriage return before it is not laid out")
    {
        // Text saved on Windows. The carriage return must not count towards the
        // width, or where a file wraps would depend on how it was saved.
        const std::vector<std::string> lines = wrap("ab\r\ncd", 1000.0f);

        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == "ab");
        CHECK(lines[1] == "cd");
    }
}

TEST_CASE("a width of zero or less breaks only at newlines", "[render][text]")
{
    const std::vector<std::string> lines = wrap("a long line here\nand another", 0.0f);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "a long line here");
    CHECK(lines[1] == "and another");
}

TEST_CASE("wrapping counts code points and not bytes", "[render][text]")
{
    trace_step("six Cyrillic letters occupy twelve bytes and must count as six");

    // The case that a byte-oriented wrapper passes on English and fails on the
    // language this engine is being written in: measured by bytes, "Привет" is
    // 120 wide and would be broken in the middle of a letter.
    const std::vector<std::string> lines = wrap("Привет мир", 60.0f);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "Привет");
    CHECK(lines[1] == "мир");
}

TEST_CASE("breaks follow the advances rather than the character count",
          "[render][text]")
{
    // Fifty wide holds five ordinary characters, but "iii" is only twelve, so more
    // of the narrow word fits than a count would allow — and "W" alone is twenty.
    const std::vector<std::string_view> lines =
        wrap_text("iiii WW", 50.0f, proportional_advance);

    for (const std::string_view line : lines)
    {
        trace("| {}", line);
    }

    // "iiii " is 16 + a space of 10 = 26, and "WW" adds 40, so the second W
    // overflows and the break falls at the space.
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "iiii");
    CHECK(lines[1] == "WW");
}
