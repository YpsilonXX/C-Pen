#include <catch2/catch_test_macros.hpp>

#include "cpen/core/value.hh"
#include "cpen/script/chunk.hh"
#include "support/trace.hh"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

using cpen::core::Value;
using cpen::script::Chunk;
using cpen::script::disassemble;
using cpen::script::instruction_size;
using cpen::script::MenuTable;
using cpen::script::OpCode;
using cpen::script::OPCODE_COUNT;
using cpen::script::operand_count;
using cpen::script::operand_kind;
using cpen::script::OperandKind;
using cpen::script::SourceSpan;
using cpen::script::to_string;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    constexpr SourceSpan SOMEWHERE{.offset = 0, .length = 1};

    /// Squeezes the padding out of a disassembly, so that a case pins what the
    /// instructions are rather than how wide the widest opcode name happens to be.
    std::string compact(const std::string& listing)
    {
        std::string result;
        bool at_line_start = true;
        bool pending_space = false;

        for (const char character : listing)
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
}

TEST_CASE("a chunk disassembles to the expected listing", "[script][chunk]")
{
    Chunk chunk;
    chunk.emit(OpCode::PUSH_CONSTANT, chunk.add_constant(Value(1)), SOMEWHERE);
    chunk.emit(OpCode::PUSH_CONSTANT, chunk.add_constant(Value(2)), SOMEWHERE);
    chunk.emit(OpCode::ADD, SOMEWHERE);
    chunk.emit(OpCode::STORE_GLOBAL, chunk.add_global("sympathy"), SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);

    trace("\n{}", disassemble(chunk));

    // An operand is four bytes, so the offsets step by five where there is one and
    // by one where there is not.
    REQUIRE(compact(disassemble(chunk)) ==
            "0000 PUSH_CONSTANT 0 ; 1\n"
            "0005 PUSH_CONSTANT 1 ; 2\n"
            "000a ADD\n"
            "000b STORE_GLOBAL 0 ; sympathy\n"
            "0010 HALT\n");
}

TEST_CASE("a forward jump is patched once its target is known", "[script][chunk]")
{
    Chunk chunk;

    // What compiling an `if` looks like: the jump is emitted before anybody knows
    // where it goes, and filled in when the block it skips has been written.
    const std::uint32_t jump = chunk.emit(OpCode::JUMP_IF_FALSE, 0, SOMEWHERE);
    chunk.emit(OpCode::POP, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);
    chunk.patch_address(jump, chunk.size());

    REQUIRE(chunk.read_operand(jump, 0) == chunk.size());
    REQUIRE(compact(disassemble(chunk)) ==
            "0000 JUMP_IF_FALSE -> 0007\n"
            "0005 POP\n"
            "0006 HALT\n");
}

TEST_CASE("constants are interned but not conflated", "[script][chunk]")
{
    Chunk chunk;

    REQUIRE(chunk.add_constant(Value(7)) == chunk.add_constant(Value(7)));

    // 1 and 1.0 are different values in this language — `1 / 2` is 0 and
    // `1.0 / 2` is 0.5 — so they must not share a constant.
    REQUIRE(chunk.add_constant(Value(1)) != chunk.add_constant(Value(1.0)));
    REQUIRE(chunk.constants().size() == 3);
}

TEST_CASE("global names are stored once per chunk", "[script][chunk]")
{
    Chunk chunk;

    REQUIRE(chunk.add_global("sympathy") == chunk.add_global("sympathy"));
    REQUIRE(chunk.add_global("shy") != chunk.add_global("sympathy"));
    REQUIRE(chunk.globals().size() == 2);
}

TEST_CASE("a label is recorded once", "[script][chunk]")
{
    Chunk chunk;

    REQUIRE(chunk.add_label("start", 0));
    REQUIRE(chunk.find_label("start").value() == 0);

    // The second one is the compiler's cue to report a duplicate rather than to
    // silently decide which of the two the script meant.
    REQUIRE_FALSE(chunk.add_label("start", 16));
    REQUIRE(chunk.find_label("start").value() == 0);
    REQUIRE_FALSE(chunk.find_label("hallway").has_value());
}

TEST_CASE("a menu table lists where each choice continues", "[script][chunk]")
{
    Chunk chunk;
    const std::uint32_t table = chunk.add_menu_table(MenuTable{.targets = {0x10, 0x20}});
    chunk.emit(OpCode::MENU, table, SOMEWHERE);

    REQUIRE(compact(disassemble(chunk)) == "0000 MENU 0 ; -> 0010 -> 0020\n");
}

TEST_CASE("every instruction remembers where it came from", "[script][chunk]")
{
    Chunk chunk;
    constexpr SourceSpan first{.offset = 10, .length = 3};
    constexpr SourceSpan second{.offset = 40, .length = 5};

    chunk.emit(OpCode::PUSH_CONSTANT, chunk.add_constant(Value(1)), first);
    chunk.emit(OpCode::HALT, second);

    trace_step("the offset an instruction begins at");
    REQUIRE(chunk.span_at(0) == first);
    REQUIRE(chunk.span_at(5) == second);

    // A run-time failure reports the instruction pointer, which may sit anywhere
    // inside the instruction that faulted.
    trace_step("an offset in the middle of an instruction");
    REQUIRE(chunk.span_at(3) == first);
}

TEST_CASE("the operand table describes every instruction", "[script][chunk]")
{
    // The disassembler reads nothing but this table, so an instruction missing
    // from it would print as an opcode with its operands rendered as further
    // instructions — a listing that is wrong from that line on.
    for (std::size_t index = 0; index < OPCODE_COUNT; ++index)
    {
        const auto instruction = static_cast<OpCode>(index);

        REQUIRE_FALSE(to_string(instruction).empty());
        REQUIRE(instruction_size(instruction) == 1 + operand_count(instruction) * 4);

        for (std::size_t operand = 0; operand < operand_count(instruction); ++operand)
        {
            REQUIRE(operand_kind(instruction, operand) != OperandKind::NONE);
        }

        REQUIRE(operand_kind(instruction, operand_count(instruction)) == OperandKind::NONE);

        // An entry naming a second operand without a first would be read as
        // taking none at all, and the instruction's own operand would be
        // disassembled as the instructions that follow it.
        REQUIRE((operand_kind(instruction, 0) != OperandKind::NONE
                 || operand_kind(instruction, 1) == OperandKind::NONE));
    }
}

TEST_CASE("reading past the end of a chunk is refused", "[script][chunk]")
{
    Chunk chunk;
    chunk.emit(OpCode::HALT, SOMEWHERE);

    REQUIRE_THROWS_AS(chunk.read_opcode(chunk.size()), std::out_of_range);

    // HALT takes no operand, so asking for one is a mistake in the caller rather
    // than a condition to be handled.
    REQUIRE_THROWS_AS(chunk.read_operand(0, 0), std::out_of_range);
}
