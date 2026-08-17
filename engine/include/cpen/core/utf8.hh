#ifndef CPEN_CORE_UTF8_HH
#define CPEN_CORE_UTF8_HH

#include <cstddef>
#include <string_view>

namespace cpen::core
{
    /// U+FFFD REPLACEMENT CHARACTER, what a malformed sequence decodes to.
    ///
    /// Substituted rather than reported, because there is nothing useful for a
    /// caller to do about it: text arrives from a script file, a save game or a
    /// player typing their name, and a dialogue line that refuses to render is a
    /// worse answer than one with a box in it. The box is also visible, which a
    /// silently dropped byte is not.
    inline constexpr char32_t REPLACEMENT_CHARACTER = 0xFFFD;

    /// The largest code point Unicode defines. Anything above is malformed however
    /// well-formed the byte sequence encoding it looks.
    inline constexpr char32_t MAXIMUM_CODE_POINT = 0x10FFFF;

    /// One code point and the number of bytes it occupied.
    struct DecodedCodePoint
    {
        char32_t code_point = REPLACEMENT_CHARACTER;

        /// Bytes consumed. Zero only at or past the end of the input, which is what
        /// ends a decoding loop; anything else always advances, so a malformed
        /// input cannot spin.
        std::size_t size = 0;
    };

    /// Decodes the code point beginning at `offset`.
    ///
    /// Malformed input is resynchronised rather than abandoned, and how much is
    /// consumed depends on what was wrong. A byte that cannot begin a sequence, and
    /// a sequence whose continuation bytes are missing or are not continuation
    /// bytes, consume one byte: the next byte might legitimately begin a sequence,
    /// and swallowing it would turn one bad byte into a run of them. A sequence
    /// that is structurally sound but means nothing — an overlong encoding, half a
    /// surrogate pair, a value past the last code point — consumes its whole length,
    /// since every byte of it was accounted for.
    ///
    /// Overlong encodings are rejected rather than accepted for the value they
    /// spell. Treating C0 80 as a NUL is the classic way a filter that checks the
    /// decoded text and a consumer that decodes it again disagree about what the
    /// bytes said.
    DecodedCodePoint decode_utf8(std::string_view text, std::size_t offset = 0) noexcept;

    /// The number of code points in `text`, malformed bytes counting as one each.
    std::size_t count_code_points(std::string_view text) noexcept;
}

#endif //CPEN_CORE_UTF8_HH
