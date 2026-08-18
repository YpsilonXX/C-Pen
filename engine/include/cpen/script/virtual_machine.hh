#ifndef CPEN_SCRIPT_VIRTUAL_MACHINE_HH
#define CPEN_SCRIPT_VIRTUAL_MACHINE_HH

#include "cpen/core/blackboard.hh"
#include "cpen/core/value.hh"
#include "cpen/script/chunk.hh"
#include "cpen/script/command_sink.hh"
#include "cpen/script/source_span.hh"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cpen::script
{
    /// Why the machine stopped, which is how the game loop knows what to do next.
    ///
    /// A novel executes in steps that wait: a line waits for the reader, a menu
    /// waits for a choice. The machine therefore never blocks and never owns the
    /// loop — it runs until it has something to wait for, says what it is waiting
    /// for, and is resumed. That, and an instruction pointer kept explicitly, is
    /// what makes a running story something a save file can describe.
    enum class YieldStatus : std::uint8_t
    {
        /// Not waiting for anything; there is more to execute.
        RUNNING,

        /// A line has been said and the reader has not gone on yet.
        WAITING_FOR_ADVANCE,

        /// A menu is on offer.
        WAITING_FOR_CHOICE,

        /// A pause is in progress.
        WAITING_FOR_TIME,

        /// The script ended.
        FINISHED,

        /// The script did something the machine cannot carry out.
        FAULTED,
    };

    std::string_view to_string(YieldStatus status) noexcept;

    /// The reader went on.
    struct AdvanceEvent
    {
    };

    /// The player took the choice at this index.
    struct ChoiceEvent
    {
        std::uint32_t index = 0;
    };

    /// The pause elapsed.
    struct TimeElapsedEvent
    {
    };

    using ResumeEvent = std::variant<AdvanceEvent, ChoiceEvent, TimeElapsedEvent>;

    /// A failure the script caused: dividing by zero, adding a number to nil.
    ///
    /// Not a diagnostic, because nothing is wrong with the file as written — the
    /// values it met at run time were. It carries the span anyway, since the only
    /// useful thing to tell an author is which line was executing.
    struct RuntimeFault
    {
        std::string message{};
        SourceSpan span{};
        std::uint32_t address = 0;
    };

    /// Runs a compiled script.
    ///
    /// Holds the chunk, the variable store and the sink by reference; all three
    /// must outlive the machine. Nothing here touches the presentation layer, and
    /// the class has no idea whether a window exists.
    ///
    /// Variables live in the blackboard rather than in a private map, so that a
    /// mini-game or a C++ behaviour reads exactly what the script wrote. The
    /// chunk's global names are interned once, when the machine is built, and
    /// addressed by identifier afterwards.
    class VirtualMachine
    {
    public:
        /// How deep calls may nest before the machine calls it runaway recursion.
        /// A novel nests a handful of scenes; anything approaching this is a
        /// script calling itself, and reporting that beats growing until the
        /// process dies.
        static constexpr std::size_t MAXIMUM_CALL_DEPTH = 256;

        VirtualMachine(const Chunk& chunk, core::Blackboard& variables, CommandSink& sink);

        /// Executes until the machine waits, finishes or faults.
        YieldStatus run();

        /// Executes one instruction. Exists for tests and for a trace; nothing in
        /// the game loop needs it.
        YieldStatus step();

        /// Resumes from a wait. Returns false if the event does not answer what
        /// the machine is waiting for, or if a choice is out of range — both are
        /// mistakes in the caller rather than in the script, and neither is
        /// allowed to move the instruction pointer.
        bool resume(const ResumeEvent& event);

        /// Begins execution at `address`, discarding any run in progress.
        ///
        /// The seam through which C++ will one day invoke a script function by
        /// name: starting a script and entering a routine are the same operation,
        /// differing only in where they begin.
        void start(std::uint32_t address = 0);

        /// Begins execution at a label. False, and no change, if there is no such
        /// label.
        bool start_at_label(std::string_view name);

        YieldStatus status() const noexcept { return this->state; }
        std::uint32_t instruction_pointer() const noexcept { return this->ip; }
        const std::optional<RuntimeFault>& fault() const noexcept { return this->failure; }

        /// Seconds left to wait. Meaningful only while WAITING_FOR_TIME.
        double wait_duration() const noexcept { return this->pending_wait; }

        /// How many choices are on offer. Meaningful only while
        /// WAITING_FOR_CHOICE.
        std::size_t choice_count() const noexcept;

        /// The value stack, outermost first. Exposed for tests and for the
        /// eventual save file, not for the running game.
        const std::vector<core::Value>& stack() const noexcept { return this->values; }

        std::size_t call_depth() const noexcept { return this->frames.size(); }

    private:
        /// One call in progress.
        ///
        /// Carries only a return address today. The base is where this call's
        /// values begin on the stack: with no parameters and no locals it is
        /// simply where to cut back to on return, and it is the same field a
        /// function's slots will be addressed from when the language grows them.
        struct CallFrame
        {
            std::uint32_t return_address = 0;
            std::uint32_t stack_base = 0;
        };

        template <typename... Arguments>
        void raise(std::uint32_t address, std::format_string<Arguments...> format,
                   Arguments&&... arguments);

        bool require(std::size_t count, std::uint32_t address);
        core::Value pop();
        const core::Value& peek() const;

        void execute(OpCode instruction, std::uint32_t address);
        void execute_arithmetic(OpCode instruction, std::uint32_t address);
        void execute_comparison(OpCode instruction, std::uint32_t address);
        void execute_concatenate(std::uint32_t count, std::uint32_t address);
        void execute_say(std::uint32_t speaker, std::uint32_t address);
        void execute_menu(std::uint32_t table, std::uint32_t address);
        void execute_presentation(OpCode instruction, std::uint32_t index, std::uint32_t address);
        void execute_call(std::uint32_t target, std::uint32_t address);
        void execute_return();

        const Chunk& chunk;
        core::Blackboard& variables;
        CommandSink& sink;

        /// One identifier per name in the chunk, resolved once rather than looked
        /// up by string on every access.
        std::vector<core::SymbolId> symbols;

        std::uint32_t ip = 0;
        YieldStatus state = YieldStatus::RUNNING;
        std::optional<RuntimeFault> failure;

        std::vector<core::Value> values;
        std::vector<CallFrame> frames;

        double pending_wait = 0.0;
        std::optional<std::uint32_t> offered_menu;
    };
}

#endif //CPEN_SCRIPT_VIRTUAL_MACHINE_HH
