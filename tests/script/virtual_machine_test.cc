#include <catch2/catch_test_macros.hpp>

#include "cpen/core/blackboard.hh"
#include "cpen/core/value.hh"
#include "cpen/script/chunk.hh"
#include "cpen/script/command_sink.hh"
#include "cpen/script/virtual_machine.hh"
#include "support/recording_sink.hh"
#include "support/trace.hh"
#include "support/value_printing.hh"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using cpen::core::Blackboard;
using cpen::core::Value;
using cpen::script::AdvanceEvent;
using cpen::script::ChoiceEvent;
using cpen::script::Chunk;
using cpen::script::MenuTable;
using cpen::script::OpCode;
using cpen::script::SourceSpan;
using cpen::script::TimeElapsedEvent;
using cpen::script::VirtualMachine;
using cpen::script::YieldStatus;
using cpen::test::RecordingSink;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    constexpr SourceSpan SOMEWHERE{.offset = 0, .length = 1};

    std::uint32_t push_constant(Chunk& chunk, Value value)
    {
        return chunk.emit(OpCode::PUSH_CONSTANT, chunk.add_constant(std::move(value)), SOMEWHERE);
    }

    /// Runs a chunk that computes one value and leaves it on the stack.
    Value evaluate(Chunk& chunk)
    {
        Blackboard variables;
        RecordingSink sink;
        VirtualMachine machine(chunk, variables, sink);

        REQUIRE(machine.run() == YieldStatus::FINISHED);
        REQUIRE(machine.stack().size() == 1);

        return machine.stack().back();
    }

    /// Builds `left <operation> right` and runs it.
    Value apply(const OpCode operation, Value left, Value right)
    {
        Chunk chunk;
        push_constant(chunk, std::move(left));
        push_constant(chunk, std::move(right));
        chunk.emit(operation, SOMEWHERE);
        chunk.emit(OpCode::HALT, SOMEWHERE);

        return evaluate(chunk);
    }

    /// Builds the same and expects it to fault, returning the message.
    std::string fault_of(const OpCode operation, Value left, Value right)
    {
        Chunk chunk;
        push_constant(chunk, std::move(left));
        push_constant(chunk, std::move(right));
        chunk.emit(operation, SOMEWHERE);
        chunk.emit(OpCode::HALT, SOMEWHERE);

        Blackboard variables;
        RecordingSink sink;
        VirtualMachine machine(chunk, variables, sink);

        REQUIRE(machine.run() == YieldStatus::FAULTED);
        REQUIRE(machine.fault().has_value());
        trace("faulted: {}", machine.fault()->message);

        return machine.fault()->message;
    }

    bool contains(const std::string_view haystack, const std::string_view needle)
    {
        return haystack.find(needle) != std::string_view::npos;
    }
}

TEST_CASE("arithmetic keeps integers integral and promotes what is mixed", "[script][vm]")
{
    trace_step("integers stay integers");
    REQUIRE(apply(OpCode::ADD, Value(1), Value(2)) == Value(3));
    REQUIRE(apply(OpCode::DIVIDE, Value(1), Value(2)) == Value(0));
    REQUIRE(apply(OpCode::REMAINDER, Value(7), Value(3)) == Value(1));

    trace_step("a mixed pair promotes");
    REQUIRE(apply(OpCode::DIVIDE, Value(1.0), Value(2)) == Value(0.5));
    REQUIRE(apply(OpCode::ADD, Value(1), Value(0.5)) == Value(1.5));
}

TEST_CASE("division by zero is refused rather than made infinite", "[script][vm]")
{
    trace_step("integers");
    REQUIRE(contains(fault_of(OpCode::DIVIDE, Value(1), Value(0)), "division by zero"));

    // A number printing as "inf" halfway through a line is a worse way to learn
    // about the mistake than a fault naming the line it happened on.
    trace_step("floating point");
    REQUIRE(contains(fault_of(OpCode::DIVIDE, Value(1.0), Value(0.0)), "division by zero"));
}

TEST_CASE("text is joined by + and never ordered", "[script][vm]")
{
    REQUIRE(apply(OpCode::ADD, Value("Привет, "), Value("мир")) == Value("Привет, мир"));

    trace_step("a number and a piece of text do not add");
    REQUIRE(contains(fault_of(OpCode::ADD, Value(1), Value("2")), "needs two numbers"));

    // Ordering text would have to answer whether "Ё" comes before "Я", and the
    // engine carries no collation to answer with.
    trace_step("text has no order");
    REQUIRE(contains(fault_of(OpCode::LESS, Value("а"), Value("б")), "needs two numbers"));
}

TEST_CASE("equality promotes across the numeric types", "[script][vm]")
{
    // Value's own equality is strict about type, and everything else in the engine
    // compares with it. The language's == is the one place that is looser.
    REQUIRE(apply(OpCode::EQUAL, Value(1), Value(1.0)) == Value(true));
    REQUIRE(apply(OpCode::EQUAL, Value(1), Value("1")) == Value(false));
    REQUIRE(apply(OpCode::NOT_EQUAL, Value(2), Value(1.0)) == Value(true));
}

TEST_CASE("only nil and false are false", "[script][vm]")
{
    const auto jumps_over = [](Value condition)
    {
        Chunk chunk;
        push_constant(chunk, std::move(condition));
        const std::uint32_t branch = chunk.emit(OpCode::JUMP_IF_FALSE, 0, SOMEWHERE);
        chunk.emit(OpCode::POP, SOMEWHERE);
        push_constant(chunk, Value("taken"));
        const std::uint32_t halt = chunk.emit(OpCode::HALT, SOMEWHERE);
        chunk.patch_address(branch, halt);

        Blackboard variables;
        RecordingSink sink;
        VirtualMachine machine(chunk, variables, sink);
        REQUIRE(machine.run() == YieldStatus::FINISHED);

        return machine.stack().back() != Value("taken");
    };

    trace_step("false");
    REQUIRE(jumps_over(Value(false)));
    REQUIRE(jumps_over(Value()));

    // Zero and the empty text are true on purpose: a counter that legitimately
    // reaches zero must not silently take the other branch.
    trace_step("true");
    REQUIRE_FALSE(jumps_over(Value(0)));
    REQUIRE_FALSE(jumps_over(Value("")));
    REQUIRE_FALSE(jumps_over(Value(true)));
}

TEST_CASE("interpolation joins values as they read in a line", "[script][vm]")
{
    Chunk chunk;
    push_constant(chunk, Value("У тебя "));
    push_constant(chunk, Value(3));
    push_constant(chunk, Value(" и "));
    push_constant(chunk, Value());
    chunk.emit(OpCode::CONCATENATE, 4, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);

    // An unset variable reads as "nil" rather than stopping the scene: a visible
    // oddity in a line gets noticed and fixed, a fault mid-chapter does not.
    REQUIRE(evaluate(chunk) == Value("У тебя 3 и nil"));
}

TEST_CASE("variables are read and written through the blackboard", "[script][vm]")
{
    Chunk chunk;
    const std::uint32_t sympathy = chunk.add_global("sympathy");
    push_constant(chunk, Value(2));
    chunk.emit(OpCode::STORE_GLOBAL, sympathy, SOMEWHERE);
    chunk.emit(OpCode::LOAD_GLOBAL, sympathy, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    REQUIRE(machine.run() == YieldStatus::FINISHED);
    REQUIRE(machine.stack().back() == Value(2));

    // The store is shared rather than private, which is what will let a mini-game
    // or a C++ behaviour read exactly what the script wrote.
    REQUIRE(variables.get("sympathy") == Value(2));
}

TEST_CASE("a line waits for the reader", "[script][vm]")
{
    Chunk chunk;
    const std::uint32_t alice = chunk.add_constant(Value("alice"));
    push_constant(chunk, Value("Привет!"));
    chunk.emit(OpCode::SAY, alice, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    REQUIRE(machine.run() == YieldStatus::WAITING_FOR_ADVANCE);
    REQUIRE(sink.lines.size() == 1);
    REQUIRE(sink.lines.front().speaker.value() == "alice");
    REQUIRE(sink.lines.front().text == "Привет!");

    trace_step("the wrong event does not move the machine");
    REQUIRE_FALSE(machine.resume(ChoiceEvent{.index = 0}));
    REQUIRE(machine.status() == YieldStatus::WAITING_FOR_ADVANCE);

    trace_step("the right one does");
    REQUIRE(machine.resume(AdvanceEvent{}));
    REQUIRE(machine.run() == YieldStatus::FINISHED);
}

TEST_CASE("narration has no speaker", "[script][vm]")
{
    Chunk chunk;
    const std::uint32_t nobody = chunk.add_constant(Value());
    push_constant(chunk, Value("Дверь скрипнула."));
    chunk.emit(OpCode::SAY, nobody, SOMEWHERE);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    REQUIRE(machine.run() == YieldStatus::WAITING_FOR_ADVANCE);
    REQUIRE_FALSE(sink.lines.front().speaker.has_value());
}

TEST_CASE("a pause waits for time to pass", "[script][vm]")
{
    Chunk chunk;
    push_constant(chunk, Value(0.5));
    chunk.emit(OpCode::PAUSE, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    REQUIRE(machine.run() == YieldStatus::WAITING_FOR_TIME);
    REQUIRE(machine.wait_duration() == 0.5);
    REQUIRE(machine.resume(TimeElapsedEvent{}));
    REQUIRE(machine.run() == YieldStatus::FINISHED);
}

TEST_CASE("a menu offers its choices and continues where the player chose", "[script][vm]")
{
    Chunk chunk;
    const std::uint32_t entry = chunk.emit(OpCode::JUMP, 0, SOMEWHERE);
    const std::uint32_t chosen = chunk.add_global("chosen");

    const std::uint32_t first_branch = push_constant(chunk, Value(10));
    chunk.emit(OpCode::STORE_GLOBAL, chosen, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);

    const std::uint32_t second_branch = push_constant(chunk, Value(20));
    chunk.emit(OpCode::STORE_GLOBAL, chosen, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);

    const std::uint32_t table = chunk.add_menu_table(MenuTable{
        .targets = {first_branch, second_branch},
    });

    // Prompts are pushed in the order the choices are written.
    const std::uint32_t menu_start = push_constant(chunk, Value("Поздороваться"));
    push_constant(chunk, Value("Промолчать"));
    chunk.emit(OpCode::MENU, table, SOMEWHERE);
    chunk.patch_address(entry, menu_start);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    REQUIRE(machine.run() == YieldStatus::WAITING_FOR_CHOICE);
    REQUIRE(machine.choice_count() == 2);
    REQUIRE(sink.menus.size() == 1);
    REQUIRE(sink.menus.front().prompts == std::vector<std::string>{"Поздороваться", "Промолчать"});

    trace_step("a choice nobody offered is refused");
    REQUIRE_FALSE(machine.resume(ChoiceEvent{.index = 2}));
    REQUIRE(machine.status() == YieldStatus::WAITING_FOR_CHOICE);

    trace_step("the second choice");
    REQUIRE(machine.resume(ChoiceEvent{.index = 1}));
    REQUIRE(machine.run() == YieldStatus::FINISHED);
    REQUIRE(variables.get("chosen") == Value(20));

    // The prompts were consumed by the menu, not left behind for whatever runs
    // next to trip over.
    REQUIRE(machine.stack().empty());
}

TEST_CASE("a call comes back to where it was made", "[script][vm]")
{
    Chunk chunk;
    const std::uint32_t entry = chunk.emit(OpCode::JUMP, 0, SOMEWHERE);
    const std::uint32_t visited = chunk.add_global("visited");

    // The routine leaves a value behind on purpose.
    const std::uint32_t routine = push_constant(chunk, Value(99));
    chunk.emit(OpCode::RETURN, SOMEWHERE);

    const std::uint32_t main = chunk.emit(OpCode::CALL, routine, SOMEWHERE);
    push_constant(chunk, Value(1));
    chunk.emit(OpCode::STORE_GLOBAL, visited, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);
    chunk.patch_address(entry, main);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    REQUIRE(machine.run() == YieldStatus::FINISHED);
    REQUIRE(variables.get("visited") == Value(1));

    // Whatever the routine left goes with its frame, so a call cannot disturb the
    // stack its caller was working on.
    REQUIRE(machine.stack().empty());
    REQUIRE(machine.call_depth() == 0);
}

TEST_CASE("a return with nothing to return to ends the script", "[script][vm]")
{
    Chunk chunk;
    chunk.emit(OpCode::RETURN, SOMEWHERE);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    REQUIRE(machine.run() == YieldStatus::FINISHED);
}

TEST_CASE("a routine that calls itself for ever is reported", "[script][vm]")
{
    Chunk chunk;
    chunk.emit(OpCode::CALL, 0, SOMEWHERE);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    // Reported rather than left to grow until the process dies.
    REQUIRE(machine.run() == YieldStatus::FAULTED);
    REQUIRE(contains(machine.fault()->message, "calling itself"));
    REQUIRE(machine.call_depth() == VirtualMachine::MAXIMUM_CALL_DEPTH);
}

TEST_CASE("a fault names the line that was running", "[script][vm]")
{
    constexpr SourceSpan first{.offset = 4, .length = 2};
    constexpr SourceSpan guilty{.offset = 40, .length = 5};

    Chunk chunk;
    chunk.emit(OpCode::PUSH_CONSTANT, chunk.add_constant(Value(1)), first);
    chunk.emit(OpCode::PUSH_CONSTANT, chunk.add_constant(Value(0)), first);
    const std::uint32_t division = chunk.emit(OpCode::DIVIDE, guilty);

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    REQUIRE(machine.run() == YieldStatus::FAULTED);
    REQUIRE(machine.fault()->span == guilty);
    REQUIRE(machine.fault()->address == division);
}

TEST_CASE("execution can begin at a label", "[script][vm]")
{
    Chunk chunk;
    const std::uint32_t reached = chunk.add_global("reached");
    chunk.emit(OpCode::HALT, SOMEWHERE);

    const std::uint32_t hallway = push_constant(chunk, Value(1));
    chunk.emit(OpCode::STORE_GLOBAL, reached, SOMEWHERE);
    chunk.emit(OpCode::HALT, SOMEWHERE);
    REQUIRE(chunk.add_label("hallway", hallway));

    Blackboard variables;
    RecordingSink sink;
    VirtualMachine machine(chunk, variables, sink);

    // The seam through which C++ will one day invoke a script routine by name:
    // starting a script and entering one are the same operation.
    REQUIRE_FALSE(machine.start_at_label("nowhere"));
    REQUIRE(machine.start_at_label("hallway"));
    REQUIRE(machine.run() == YieldStatus::FINISHED);
    REQUIRE(variables.get("reached") == Value(1));
}
