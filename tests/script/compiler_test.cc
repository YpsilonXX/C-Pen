#include <catch2/catch_test_macros.hpp>

#include "cpen/core/blackboard.hh"
#include "cpen/core/value.hh"
#include "cpen/script/chunk.hh"
#include "cpen/script/compiler.hh"
#include "cpen/script/diagnostic.hh"
#include "cpen/script/virtual_machine.hh"
#include "support/recording_sink.hh"
#include "support/trace.hh"
#include "support/value_printing.hh"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using cpen::core::Blackboard;
using cpen::core::Value;
using cpen::script::AdvanceEvent;
using cpen::script::ChoiceEvent;
using cpen::script::Chunk;
using cpen::script::compile_script;
using cpen::script::Diagnostic;
using cpen::script::disassemble;
using cpen::script::instruction_size;
using cpen::script::OpCode;
using cpen::script::to_string;
using cpen::script::VirtualMachine;
using cpen::script::YieldStatus;
using cpen::test::RecordingSink;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    Chunk build(std::string source)
    {
        std::expected<Chunk, std::vector<Diagnostic>> compiled =
            compile_script("test.pen", std::move(source));

        std::string reasons;
        if (!compiled.has_value())
        {
            for (const Diagnostic& diagnostic : compiled.error())
            {
                reasons += diagnostic.message + "\n";
            }
        }

        INFO(reasons);
        REQUIRE(compiled.has_value());

        trace("\n{}", disassemble(*compiled));
        return std::move(*compiled);
    }

    std::string errors_of(std::string source)
    {
        const std::expected<Chunk, std::vector<Diagnostic>> compiled =
            compile_script("test.pen", std::move(source));

        REQUIRE_FALSE(compiled.has_value());

        std::string joined;
        for (const Diagnostic& diagnostic : compiled.error())
        {
            joined += diagnostic.message + "\n";
        }

        trace("{}", joined);
        return joined;
    }

    /// The instructions in order, without their offsets.
    ///
    /// What most of these cases are about is the shape of the code the compiler
    /// laid down; the offsets are arithmetic over instruction sizes, and pinning
    /// them by hand would make every case a puzzle without testing anything more.
    std::vector<std::string> opcodes_of(const Chunk& chunk)
    {
        std::vector<std::string> names;

        std::uint32_t offset = 0;
        while (offset < chunk.size())
        {
            const OpCode instruction = chunk.read_opcode(offset);
            names.emplace_back(to_string(instruction));
            offset += static_cast<std::uint32_t>(instruction_size(instruction));
        }

        return names;
    }

    bool contains(const std::string_view haystack, const std::string_view needle)
    {
        return haystack.find(needle) != std::string_view::npos;
    }
}

TEST_CASE("an assignment compiles to the expected bytecode", "[script][compiler]")
{
    const Chunk chunk = build("$ x = 1 + 2\n");

    // The one case with offsets written out, small enough to be read at a glance:
    // an operand is four bytes, so an instruction with one is five and an
    // instruction without is one.
    REQUIRE(disassemble(chunk) ==
            "0000 PUSH_CONSTANT      0  ; 1\n"
            "0005 PUSH_CONSTANT      1  ; 2\n"
            "000a ADD\n"
            "000b STORE_GLOBAL       0  ; x\n"
            "0010 HALT\n");
}

TEST_CASE("a label is stepped over and ends in a return", "[script][compiler]")
{
    const Chunk chunk = build("label start:\n\tjump start\n");

    REQUIRE(opcodes_of(chunk) == std::vector<std::string>{"JUMP", "JUMP", "RETURN", "HALT"});

    // The first jump steps over the body: a label is entered by jump or call and
    // by nothing else, which is what stops the order of labels in a file from
    // meaning anything.
    REQUIRE(chunk.find_label("start").value() == 5);
    REQUIRE(chunk.read_operand(0, 0) == 11);
    REQUIRE(chunk.read_operand(5, 0) == 5);
}

TEST_CASE("a conditional compiles to jumps around its branches", "[script][compiler]")
{
    const Chunk chunk = build("if x:\n"
                              "\t$ y = 1\n"
                              "else:\n"
                              "\t$ y = 2\n");

    REQUIRE(opcodes_of(chunk) == std::vector<std::string>{
                "LOAD_GLOBAL",   // the condition
                "JUMP_IF_FALSE",
                "POP",           // taken: discard the condition
                "PUSH_CONSTANT", "STORE_GLOBAL",
                "JUMP",          // over the else
                "POP",           // not taken: discard the condition
                "PUSH_CONSTANT", "STORE_GLOBAL",
                "HALT",
            });
}

TEST_CASE("and compiles to a jump rather than to an instruction", "[script][compiler]")
{
    const Chunk chunk = build("$ x = a and b\n");

    // The value that decided the result is left where it is, which is why the
    // conditional jumps read the stack without popping.
    REQUIRE(opcodes_of(chunk) == std::vector<std::string>{
                "LOAD_GLOBAL", "JUMP_IF_FALSE", "POP", "LOAD_GLOBAL",
                "STORE_GLOBAL", "HALT",
            });
}

TEST_CASE("a line without interpolation is one constant", "[script][compiler]")
{
    trace_step("plain");
    {
        const Chunk chunk = build("\"Дверь скрипнула.\"\n");

        REQUIRE(opcodes_of(chunk) == std::vector<std::string>{"PUSH_CONSTANT", "SAY", "HALT"});

        // Read through the instruction rather than off the front of the pool: the
        // speaker's constant is added before the line is compiled, and for
        // narration that speaker is nil, so the line is not the first entry.
        REQUIRE(chunk.constants()[chunk.read_operand(0, 0)] == Value("Дверь скрипнула."));
    }

    trace_step("interpolated");
    {
        const Chunk chunk = build("\"У тебя /{sympathy} очков.\"\n");

        REQUIRE(opcodes_of(chunk) == std::vector<std::string>{
                    "PUSH_CONSTANT", "LOAD_GLOBAL", "PUSH_CONSTANT", "CONCATENATE",
                    "SAY", "HALT",
                });
    }
}

TEST_CASE("the words of a show become an asset name", "[script][compiler]")
{
    trace_step("by anchor");
    {
        const Chunk chunk = build("show alice happy at left with dissolve\n");

        REQUIRE(chunk.commands().size() == 1);
        REQUIRE(chunk.commands().front().target == "alice/happy");
        REQUIRE(chunk.commands().front().anchor == "left");
        REQUIRE(chunk.commands().front().transition == "dissolve");
        REQUIRE_FALSE(chunk.commands().front().position_on_stack);
    }

    trace_step("by coordinates");
    {
        const Chunk chunk = build("show alice at (0.5, 0.8)\n");

        // The coordinates are computed, so they are pushed before the instruction
        // that consumes them and the record says to expect them.
        REQUIRE(opcodes_of(chunk) == std::vector<std::string>{
                    "PUSH_CONSTANT", "PUSH_CONSTANT", "SHOW", "HALT",
                });
        REQUIRE(chunk.commands().front().position_on_stack);
    }
}

TEST_CASE("a menu knows where each choice continues", "[script][compiler]")
{
    const Chunk chunk = build("menu:\n"
                              "\t\"Уйти\":\n"
                              "\t\t$ x = 1\n"
                              "\t\"Остаться\":\n"
                              "\t\t$ x = 2\n");

    REQUIRE(chunk.menu_tables().size() == 1);
    REQUIRE(chunk.menu_tables().front().targets.size() == 2);

    // Both blocks are compiled after the instruction that offers them, and each
    // ends by jumping past the other.
    const std::vector<std::uint32_t>& targets = chunk.menu_tables().front().targets;
    REQUIRE(targets[0] < targets[1]);
    REQUIRE(chunk.read_opcode(targets[0]) == OpCode::PUSH_CONSTANT);
    REQUIRE(chunk.read_opcode(targets[1]) == OpCode::PUSH_CONSTANT);
}

TEST_CASE("what the compiler refuses", "[script][compiler]")
{
    trace_step("a label inside another label");
    REQUIRE(contains(errors_of("label chapter:\n\t\"А.\"\n\tlabel part:\n\t\t\"Б.\"\n"),
                     "may only appear at the outermost level"));

    trace_step("the same label twice");
    REQUIRE(contains(errors_of("label start:\n\t\"А.\"\nlabel start:\n\t\"Б.\"\n"),
                     "already a label called 'start'"));

    trace_step("a jump to nowhere");
    REQUIRE(contains(errors_of("jump nowhere\n"), "no label called 'nowhere'"));

    trace_step("a call to nowhere");
    REQUIRE(contains(errors_of("call nowhere\n"), "no label called 'nowhere'"));
}

TEST_CASE("a script jumps forward as freely as back", "[script][compiler]")
{
    // Every label reference is resolved after the whole file is compiled, so the
    // order labels appear in is not part of the meaning of a script.
    const Chunk chunk = build("label first:\n"
                              "\tjump second\n"
                              "label second:\n"
                              "\t\"Конец.\"\n");

    REQUIRE(chunk.find_label("second").has_value());
    REQUIRE(chunk.read_operand(5, 0) == chunk.find_label("second").value());
}

TEST_CASE("a whole script runs from source to recorded commands", "[script][compiler]")
{
    // The end-to-end case for the phase: text in, a played scene out, with the
    // lexer, the parser, the compiler and the machine all in the path and nothing
    // graphical anywhere near it.
    const Chunk chunk = build("label start:\n"
                              "\tscene bg_room with fade\n"
                              "\tshow alice happy at left\n"
                              "\t$ sympathy = 0\n"
                              "\talice \"Привет!\"\n"
                              "\tmenu:\n"
                              "\t\t\"Поздороваться\":\n"
                              "\t\t\t$ sympathy = sympathy + 1\n"
                              "\t\t\talice \"И тебе привет.\"\n"
                              "\t\t\"Промолчать\":\n"
                              "\t\t\talice \"...\"\n"
                              "\tif sympathy > 0:\n"
                              "\t\talice \"У тебя /{sympathy} очков.\"\n"
                              "\telse:\n"
                              "\t\talice \"Ну и ладно.\"\n"
                              "\thide alice with dissolve\n");

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    // A file of nothing but labels does nothing until something starts it at one.
    trace_step("running from the beginning falls straight through");
    REQUIRE(machine.run() == YieldStatus::FINISHED);
    REQUIRE(sink.lines.empty());

    trace_step("the first line, after the scene is set");
    REQUIRE(machine.start_at_label("start"));
    REQUIRE(machine.run() == YieldStatus::WAITING_FOR_ADVANCE);

    REQUIRE(sink.scenes.size() == 1);
    REQUIRE(sink.scenes.front().background == "bg_room");
    REQUIRE(sink.scenes.front().transition == "fade");
    REQUIRE(sink.shown.size() == 1);
    REQUIRE(sink.shown.front().asset == "alice/happy");
    REQUIRE(sink.shown.front().anchor == "left");
    REQUIRE(sink.lines.back().speaker.value() == "alice");
    REQUIRE(sink.lines.back().text == "Привет!");

    trace_step("the menu");
    REQUIRE(machine.resume(AdvanceEvent{}));
    REQUIRE(machine.run() == YieldStatus::WAITING_FOR_CHOICE);
    REQUIRE(sink.menus.front().prompts
            == std::vector<std::string>{"Поздороваться", "Промолчать"});

    trace_step("greeting her");
    REQUIRE(machine.resume(ChoiceEvent{.index = 0}));
    REQUIRE(machine.run() == YieldStatus::WAITING_FOR_ADVANCE);
    REQUIRE(sink.lines.back().text == "И тебе привет.");
    REQUIRE(variables.get("sympathy") == Value(1));

    trace_step("the branch that the choice earned, with the count read into the line");
    REQUIRE(machine.resume(AdvanceEvent{}));
    REQUIRE(machine.run() == YieldStatus::WAITING_FOR_ADVANCE);
    REQUIRE(sink.lines.back().text == "У тебя 1 очков.");

    trace_step("and the end of the label returns out of the script");
    REQUIRE(machine.resume(AdvanceEvent{}));
    REQUIRE(machine.run() == YieldStatus::FINISHED);
    REQUIRE(sink.hidden.size() == 1);
    REQUIRE(sink.hidden.front().name == "alice");
    REQUIRE(sink.hidden.front().transition == "dissolve");
}

TEST_CASE("a shared scene is called from two places and returns to each", "[script][compiler]")
{
    const Chunk chunk = build("label kitchen:\n"
                              "\tcall shared\n"
                              "\t\"Вернулись на кухню.\"\n"
                              "label hall:\n"
                              "\tcall shared\n"
                              "\t\"Вернулись в холл.\"\n"
                              "label shared:\n"
                              "\t\"Свет мигнул.\"\n");

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    const auto play = [&](const std::string_view label)
    {
        REQUIRE(machine.start_at_label(label));

        while (machine.run() == YieldStatus::WAITING_FOR_ADVANCE)
        {
            REQUIRE(machine.resume(AdvanceEvent{}));
        }

        REQUIRE(machine.status() == YieldStatus::FINISHED);
    };

    play("kitchen");
    play("hall");

    // The shared label ends without saying where to go, and each caller still
    // continued where it left off -- which is the whole reason call exists.
    REQUIRE(sink.lines.size() == 4);
    REQUIRE(sink.lines[0].text == "Свет мигнул.");
    REQUIRE(sink.lines[1].text == "Вернулись на кухню.");
    REQUIRE(sink.lines[2].text == "Свет мигнул.");
    REQUIRE(sink.lines[3].text == "Вернулись в холл.");
}
