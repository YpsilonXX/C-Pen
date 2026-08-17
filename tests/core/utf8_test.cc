#include <catch2/catch_test_macros.hpp>

#include "cpen/core/utf8.hh"
#include "support/trace.hh"

#include <string>
#include <string_view>
#include <vector>

using cpen::core::count_code_points;
using cpen::core::decode_utf8;
using cpen::core::DecodedCodePoint;
using cpen::core::REPLACEMENT_CHARACTER;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    /// Decodes a whole string, so a case can state what it expects as a list rather
    /// than as a loop.
    std::vector<char32_t> decode_all(const std::string_view text)
    {
        std::vector<char32_t> points;
        std::size_t offset = 0;

        while (offset < text.size())
        {
            const DecodedCodePoint decoded = decode_utf8(text, offset);

            // The property the whole loop rests on: a malformed input must still
            // advance, or a decoding loop spins on it forever.
            REQUIRE(decoded.size > 0);

            points.push_back(decoded.code_point);
            offset += decoded.size;
        }

        return points;
    }

    void trace_points(const std::vector<char32_t>& points)
    {
        std::string listed;
        for (const char32_t point : points)
        {
            listed += std::format("U+{:04X} ", static_cast<std::uint32_t>(point));
        }
        trace("decoded: {}", listed);
    }
}

TEST_CASE("ascii decodes one byte at a time", "[core][utf8]")
{
    const std::vector<char32_t> points = decode_all("Hi!");
    trace_points(points);

    REQUIRE(points == std::vector<char32_t>{'H', 'i', '!'});
    CHECK(count_code_points("Hi!") == 3);
}

TEST_CASE("cyrillic decodes as two bytes per letter", "[core][utf8]")
{
    // The engine's own text is Russian, so this is the case that matters most and
    // the one a byte-counting length would get wrong: six letters in twelve bytes.
    constexpr std::string_view text = "Привет";

    const std::vector<char32_t> points = decode_all(text);
    trace_points(points);

    trace("{} byte(s) hold {} code point(s)", text.size(), count_code_points(text));

    CHECK(text.size() == 12);
    CHECK(points.size() == 6);
    CHECK(points.front() == 0x041F);  // П
    CHECK(points.back() == 0x0442);   // т
}

TEST_CASE("sequences of every length decode to their code point", "[core][utf8]")
{
    SECTION("two bytes")
    {
        const auto decoded = decode_utf8("\xC2\xA9");  // U+00A9 ©
        trace("two-byte sequence: U+{:04X} in {} byte(s)",
              static_cast<std::uint32_t>(decoded.code_point), decoded.size);

        CHECK(decoded.code_point == 0x00A9);
        CHECK(decoded.size == 2);
    }

    SECTION("three bytes")
    {
        const auto decoded = decode_utf8("\xE2\x80\x94");  // U+2014 em dash
        trace("three-byte sequence: U+{:04X} in {} byte(s)",
              static_cast<std::uint32_t>(decoded.code_point), decoded.size);

        CHECK(decoded.code_point == 0x2014);
        CHECK(decoded.size == 3);
    }

    SECTION("four bytes")
    {
        const auto decoded = decode_utf8("\xF0\x9F\x99\x82");  // U+1F642
        trace("four-byte sequence: U+{:04X} in {} byte(s)",
              static_cast<std::uint32_t>(decoded.code_point), decoded.size);

        CHECK(decoded.code_point == 0x1F642);
        CHECK(decoded.size == 4);
    }
}

TEST_CASE("decoding stops at the end of the input", "[core][utf8]")
{
    // A size of zero is what ends a loop, and it is the one case where nothing is
    // consumed. Every other answer advances.
    const auto past_end = decode_utf8("abc", 3);
    CHECK(past_end.size == 0);

    const auto far_past_end = decode_utf8("abc", 99);
    CHECK(far_past_end.size == 0);

    CHECK(count_code_points("") == 0);
}

TEST_CASE("a byte that cannot begin a sequence consumes only itself", "[core][utf8]")
{
    // 0x80 is a continuation byte with nothing to continue. Consuming more than the
    // one byte would swallow the 'a' after it, turning one bad byte into two.
    const std::vector<char32_t> points = decode_all("\x80\x61");
    trace_points(points);

    REQUIRE(points.size() == 2);
    CHECK(points[0] == REPLACEMENT_CHARACTER);
    CHECK(points[1] == 'a');
}

TEST_CASE("a truncated sequence resynchronises on the next byte", "[core][utf8]")
{
    SECTION("cut short by the end of the input")
    {
        const auto decoded = decode_utf8("\xE2\x80");
        trace("two bytes of a three-byte sequence: size {}", decoded.size);

        CHECK(decoded.code_point == REPLACEMENT_CHARACTER);
        CHECK(decoded.size == 1);
    }

    SECTION("cut short by something that is not a continuation byte")
    {
        // The 'X' is not part of the sequence and must come out as itself.
        const std::vector<char32_t> points = decode_all("\xE2\x80X");
        trace_points(points);

        REQUIRE(points.size() == 3);
        CHECK(points[0] == REPLACEMENT_CHARACTER);
        CHECK(points[1] == REPLACEMENT_CHARACTER);
        CHECK(points[2] == 'X');
    }
}

TEST_CASE("an overlong encoding is refused rather than read for its value",
          "[core][utf8]")
{
    trace_step("C0 80 spells U+0000 in two bytes where one would do");

    // Accepting this is the classic way a filter that inspects decoded text and a
    // consumer that decodes the bytes again end up disagreeing about what was
    // said: the filter sees a harmless two-byte sequence, the consumer sees a NUL.
    const auto decoded = decode_utf8("\xC0\x80");

    CHECK(decoded.code_point == REPLACEMENT_CHARACTER);

    // The whole sequence, unlike the truncated cases: every byte was accounted
    // for, so there is nothing after it to resynchronise on.
    CHECK(decoded.size == 2);

    SECTION("and so is a three-byte encoding of a two-byte value")
    {
        const auto slash = decode_utf8("\xE0\x80\xAF");  // U+002F written in three
        CHECK(slash.code_point == REPLACEMENT_CHARACTER);
        CHECK(slash.size == 3);
    }
}

TEST_CASE("surrogates and out-of-range values are refused", "[core][utf8]")
{
    SECTION("half of a surrogate pair")
    {
        // U+D800. Surrogates exist only inside UTF-16 and are not code points a
        // UTF-8 stream may carry, however well-formed the three bytes look.
        const auto decoded = decode_utf8("\xED\xA0\x80");
        trace("lone surrogate: U+{:04X}, {} byte(s)",
              static_cast<std::uint32_t>(decoded.code_point), decoded.size);

        CHECK(decoded.code_point == REPLACEMENT_CHARACTER);
        CHECK(decoded.size == 3);
    }

    SECTION("past the last code point")
    {
        // U+110000, one past the end of Unicode.
        const auto decoded = decode_utf8("\xF4\x90\x80\x80");
        CHECK(decoded.code_point == REPLACEMENT_CHARACTER);
        CHECK(decoded.size == 4);
    }

    SECTION("and the last valid code point still decodes")
    {
        const auto decoded = decode_utf8("\xF4\x8F\xBF\xBF");  // U+10FFFF
        trace("largest code point: U+{:04X}",
              static_cast<std::uint32_t>(decoded.code_point));

        CHECK(decoded.code_point == 0x10FFFF);
        CHECK(decoded.size == 4);
    }
}

TEST_CASE("malformed bytes do not disturb what surrounds them", "[core][utf8]")
{
    // A realistic corruption: a Russian word with one byte of a letter lost.
    const std::string text = std::string{"да"} + "\xD0" + "нет";

    const std::vector<char32_t> points = decode_all(text);
    trace_points(points);

    // д а <bad> н е т
    REQUIRE(points.size() == 6);
    CHECK(points[0] == 0x0434);
    CHECK(points[1] == 0x0430);
    CHECK(points[2] == REPLACEMENT_CHARACTER);
    CHECK(points[3] == 0x043D);
    CHECK(points[5] == 0x0442);
}
