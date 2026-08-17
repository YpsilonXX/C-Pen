#include "cpen/core/utf8.hh"

#include <cstdint>

namespace cpen::core
{
    namespace
    {
        /// The first code point each sequence length is allowed to encode. A value
        /// below its length's entry was written in more bytes than it needed, which
        /// is the overlong form the decoder refuses.
        constexpr char32_t MINIMUM_FOR_LENGTH[] = {0, 0x0, 0x80, 0x800, 0x10000};

        constexpr char32_t FIRST_SURROGATE = 0xD800;
        constexpr char32_t LAST_SURROGATE = 0xDFFF;

        bool is_continuation(const unsigned char byte) noexcept
        {
            return (byte & 0xC0) == 0x80;
        }

        /// The number of bytes the sequence starting with `leader` claims to be, or
        /// zero if it cannot begin one at all.
        std::size_t sequence_length(const unsigned char leader) noexcept
        {
            if (leader < 0x80)
            {
                return 1;
            }
            if ((leader & 0xE0) == 0xC0)
            {
                return 2;
            }
            if ((leader & 0xF0) == 0xE0)
            {
                return 3;
            }
            if ((leader & 0xF8) == 0xF0)
            {
                return 4;
            }

            // A continuation byte where a leader was expected, or one of the two
            // values (0xFE, 0xFF) that never appear in UTF-8 at all.
            return 0;
        }

        /// The bits the leader of a `length`-byte sequence contributes.
        char32_t leader_bits(const unsigned char leader, const std::size_t length) noexcept
        {
            switch (length)
            {
                case 1: return leader;
                case 2: return leader & 0x1Fu;
                case 3: return leader & 0x0Fu;
                case 4: return leader & 0x07u;
                default: return 0;
            }
        }
    }

    DecodedCodePoint decode_utf8(const std::string_view text, const std::size_t offset) noexcept
    {
        if (offset >= text.size())
        {
            return DecodedCodePoint{.code_point = REPLACEMENT_CHARACTER, .size = 0};
        }

        const auto leader = static_cast<unsigned char>(text[offset]);
        const std::size_t length = sequence_length(leader);

        if (length == 0)
        {
            return DecodedCodePoint{.code_point = REPLACEMENT_CHARACTER, .size = 1};
        }

        if (length == 1)
        {
            return DecodedCodePoint{.code_point = leader, .size = 1};
        }

        // One byte, not `length`: the bytes after a broken leader have not been
        // examined yet, and one of them may well begin a valid sequence.
        if (offset + length > text.size())
        {
            return DecodedCodePoint{.code_point = REPLACEMENT_CHARACTER, .size = 1};
        }

        char32_t code_point = leader_bits(leader, length);

        for (std::size_t index = 1; index < length; ++index)
        {
            const auto byte = static_cast<unsigned char>(text[offset + index]);
            if (!is_continuation(byte))
            {
                return DecodedCodePoint{.code_point = REPLACEMENT_CHARACTER, .size = 1};
            }

            code_point = (code_point << 6) | (byte & 0x3Fu);
        }

        // Structurally complete from here on, so the whole sequence is consumed
        // whatever it turned out to mean.
        const bool is_overlong = code_point < MINIMUM_FOR_LENGTH[length];
        const bool is_surrogate = code_point >= FIRST_SURROGATE && code_point <= LAST_SURROGATE;
        const bool is_too_large = code_point > MAXIMUM_CODE_POINT;

        if (is_overlong || is_surrogate || is_too_large)
        {
            return DecodedCodePoint{.code_point = REPLACEMENT_CHARACTER, .size = length};
        }

        return DecodedCodePoint{.code_point = code_point, .size = length};
    }

    std::size_t count_code_points(const std::string_view text) noexcept
    {
        std::size_t count = 0;
        std::size_t offset = 0;

        while (offset < text.size())
        {
            const DecodedCodePoint decoded = decode_utf8(text, offset);
            offset += decoded.size;
            ++count;
        }

        return count;
    }
}
