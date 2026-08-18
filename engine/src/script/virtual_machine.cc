#include "cpen/script/virtual_machine.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace cpen::script
{
    namespace
    {
        template <typename... Handlers>
        struct Overloaded : Handlers...
        {
            using Handlers::operator()...;
        };

        /// nil and false are false; every other value is true.
        ///
        /// Zero and the empty text are deliberately true. The alternative is the
        /// oldest trap in scripting: a counter that legitimately reaches zero, or
        /// a name that is legitimately blank, silently takes the other branch, and
        /// the author reads the condition as asking whether the variable is set.
        bool is_true(const core::Value& value) noexcept
        {
            return !value.is_nil() && value.as_boolean().value_or(true);
        }

        /// How a value reads when it lands in a line of dialogue.
        ///
        /// Not Value::to_string, which quotes text and exists for logs. An unset
        /// variable appears as "nil" rather than stopping the scene: a visible
        /// oddity in a line is something the author sees and fixes, where a fault
        /// in the middle of a chapter is a game that cannot be played to the point
        /// where the mistake shows.
        std::string to_display_text(const core::Value& value)
        {
            return value.is_text() ? std::string(value.as_text()) : value.to_string();
        }

        /// How an instruction is written in a script, for a message an author
        /// reads. The enumerator's name is right for a disassembly and wrong here.
        std::string_view operator_spelling(const OpCode instruction) noexcept
        {
            switch (instruction)
            {
                case OpCode::ADD:              return "+";
                case OpCode::SUBTRACT:         return "-";
                case OpCode::MULTIPLY:         return "*";
                case OpCode::DIVIDE:           return "/";
                case OpCode::REMAINDER:        return "%";
                case OpCode::NEGATE:           return "-";
                case OpCode::LESS:             return "<";
                case OpCode::LESS_OR_EQUAL:    return "<=";
                case OpCode::GREATER:          return ">";
                case OpCode::GREATER_OR_EQUAL: return ">=";
                default:                       return to_string(instruction);
            }
        }
    }

    std::string_view to_string(const YieldStatus status) noexcept
    {
        switch (status)
        {
            case YieldStatus::RUNNING:             return "running";
            case YieldStatus::WAITING_FOR_ADVANCE: return "waiting for the reader";
            case YieldStatus::WAITING_FOR_CHOICE:  return "waiting for a choice";
            case YieldStatus::WAITING_FOR_TIME:    return "waiting for time to pass";
            case YieldStatus::FINISHED:            return "finished";
            case YieldStatus::FAULTED:             return "faulted";
        }
        return "unknown";
    }

    VirtualMachine::VirtualMachine(const Chunk& compiled, core::Blackboard& store,
                                   CommandSink& commands)
        : chunk(compiled),
          variables(store),
          sink(commands)
    {
        // Bound once, here, rather than by name on every access: this is the whole
        // reason the chunk carries a table of names instead of spelling one into
        // each instruction.
        this->symbols.reserve(this->chunk.globals().size());
        for (const std::string& name : this->chunk.globals())
        {
            this->symbols.push_back(this->variables.intern(name));
        }

        this->start(0);
    }

    void VirtualMachine::start(const std::uint32_t address)
    {
        this->ip = address;
        this->state = YieldStatus::RUNNING;
        this->failure.reset();
        this->values.clear();
        this->frames.clear();
        this->pending_wait = 0.0;
        this->offered_menu.reset();
    }

    bool VirtualMachine::start_at_label(const std::string_view name)
    {
        const std::optional<std::uint32_t> address = this->chunk.find_label(name);
        if (!address.has_value())
        {
            return false;
        }

        this->start(*address);
        return true;
    }

    std::size_t VirtualMachine::choice_count() const noexcept
    {
        if (!this->offered_menu.has_value()
            || *this->offered_menu >= this->chunk.menu_tables().size())
        {
            return 0;
        }

        return this->chunk.menu_tables()[*this->offered_menu].targets.size();
    }

    YieldStatus VirtualMachine::run()
    {
        while (this->state == YieldStatus::RUNNING)
        {
            this->step();
        }

        return this->state;
    }

    YieldStatus VirtualMachine::step()
    {
        if (this->state != YieldStatus::RUNNING)
        {
            return this->state;
        }

        // Running off the end ends the script. A chunk the compiler produced ends
        // in HALT or RETURN, so this is the answer for a chunk assembled by hand
        // and for one whose last instruction jumped past itself -- an ending
        // rather than a fault, because there is nothing left that could go wrong.
        if (this->ip >= this->chunk.size())
        {
            this->state = YieldStatus::FINISHED;
            return this->state;
        }

        const std::uint32_t address = this->ip;
        const OpCode instruction = this->chunk.read_opcode(address);

        // Advanced before the instruction runs, so that a jump only has to write
        // the pointer and a call only has to read it.
        this->ip = address + static_cast<std::uint32_t>(instruction_size(instruction));

        this->execute(instruction, address);
        return this->state;
    }

    bool VirtualMachine::resume(const ResumeEvent& event)
    {
        return std::visit(Overloaded{
            [&](const AdvanceEvent&)
            {
                if (this->state != YieldStatus::WAITING_FOR_ADVANCE)
                {
                    return false;
                }

                this->state = YieldStatus::RUNNING;
                return true;
            },
            [&](const ChoiceEvent& choice)
            {
                if (this->state != YieldStatus::WAITING_FOR_CHOICE
                    || !this->offered_menu.has_value())
                {
                    return false;
                }

                const MenuTable& table = this->chunk.menu_tables()[*this->offered_menu];
                if (choice.index >= table.targets.size())
                {
                    return false;
                }

                this->ip = table.targets[choice.index];
                this->offered_menu.reset();
                this->state = YieldStatus::RUNNING;
                return true;
            },
            [&](const TimeElapsedEvent&)
            {
                if (this->state != YieldStatus::WAITING_FOR_TIME)
                {
                    return false;
                }

                this->pending_wait = 0.0;
                this->state = YieldStatus::RUNNING;
                return true;
            },
        }, event);
    }

    template <typename... Arguments>
    void VirtualMachine::raise(const std::uint32_t address,
                               const std::format_string<Arguments...> format,
                               Arguments&&... arguments)
    {
        this->state = YieldStatus::FAULTED;
        this->failure = RuntimeFault{
            .message = std::format(format, std::forward<Arguments>(arguments)...),
            .span = this->chunk.span_at(address),
            .address = address,
        };
    }

    bool VirtualMachine::require(const std::size_t count, const std::uint32_t address)
    {
        if (this->values.size() >= count)
        {
            return true;
        }

        // Not something a script can cause: the compiler balances the stack. It is
        // reported rather than assumed away so that a fault in the compiler
        // surfaces as a message instead of as a read past the end of a vector.
        this->raise(address, "the stack holds {} values where {} were needed",
                    this->values.size(), count);
        return false;
    }

    core::Value VirtualMachine::pop()
    {
        core::Value value = std::move(this->values.back());
        this->values.pop_back();

        return value;
    }

    const core::Value& VirtualMachine::peek() const
    {
        return this->values.back();
    }

    void VirtualMachine::execute(const OpCode instruction, const std::uint32_t address)
    {
        // Deliberately without a default case: an instruction added to the table
        // and forgotten here is what -Wswitch exists to report, and this is the one
        // place in the machine where forgetting one has to be noticed.
        switch (instruction)
        {
            case OpCode::PUSH_CONSTANT:
            {
                const std::uint32_t index = this->chunk.read_operand(address, 0);
                if (index >= this->chunk.constants().size())
                {
                    this->raise(address, "there is no constant {}", index);
                    return;
                }

                this->values.push_back(this->chunk.constants()[index]);
                return;
            }

            case OpCode::PUSH_NIL:   this->values.emplace_back(); return;
            case OpCode::PUSH_TRUE:  this->values.emplace_back(true); return;
            case OpCode::PUSH_FALSE: this->values.emplace_back(false); return;

            case OpCode::POP:
                if (!this->require(1, address))
                {
                    return;
                }

                this->values.pop_back();
                return;

            case OpCode::LOAD_GLOBAL:
            {
                const std::uint32_t index = this->chunk.read_operand(address, 0);
                if (index >= this->symbols.size())
                {
                    this->raise(address, "there is no variable {}", index);
                    return;
                }

                this->values.push_back(this->variables.get(this->symbols[index]));
                return;
            }

            case OpCode::STORE_GLOBAL:
            {
                const std::uint32_t index = this->chunk.read_operand(address, 0);
                if (index >= this->symbols.size())
                {
                    this->raise(address, "there is no variable {}", index);
                    return;
                }

                if (!this->require(1, address))
                {
                    return;
                }

                this->variables.set(this->symbols[index], this->pop());
                return;
            }

            case OpCode::ADD:
            case OpCode::SUBTRACT:
            case OpCode::MULTIPLY:
            case OpCode::DIVIDE:
            case OpCode::REMAINDER:
            case OpCode::NEGATE:
                this->execute_arithmetic(instruction, address);
                return;

            case OpCode::EQUAL:
            case OpCode::NOT_EQUAL:
            case OpCode::LESS:
            case OpCode::LESS_OR_EQUAL:
            case OpCode::GREATER:
            case OpCode::GREATER_OR_EQUAL:
                this->execute_comparison(instruction, address);
                return;

            case OpCode::NOT:
                if (!this->require(1, address))
                {
                    return;
                }

                this->values.back() = core::Value(!is_true(this->peek()));
                return;

            case OpCode::CONCATENATE:
                this->execute_concatenate(this->chunk.read_operand(address, 0), address);
                return;

            case OpCode::JUMP:
                this->ip = this->chunk.read_operand(address, 0);
                return;

            case OpCode::JUMP_IF_FALSE:
                if (!this->require(1, address))
                {
                    return;
                }

                // The condition is read, not consumed. The compiler emits the pop,
                // which is what lets `and` and `or` leave their result in place
                // while `if` discards its condition.
                if (!is_true(this->peek()))
                {
                    this->ip = this->chunk.read_operand(address, 0);
                }

                return;

            case OpCode::JUMP_IF_TRUE:
                if (!this->require(1, address))
                {
                    return;
                }

                if (is_true(this->peek()))
                {
                    this->ip = this->chunk.read_operand(address, 0);
                }

                return;

            case OpCode::CALL:
                this->execute_call(this->chunk.read_operand(address, 0), address);
                return;

            case OpCode::RETURN:
                this->execute_return();
                return;

            case OpCode::SAY:
                this->execute_say(this->chunk.read_operand(address, 0), address);
                return;

            case OpCode::PAUSE:
            {
                if (!this->require(1, address))
                {
                    return;
                }

                const core::Value duration = this->pop();
                const std::optional<double> seconds = duration.to_floating();
                if (!seconds.has_value())
                {
                    this->raise(address, "'pause' needs a number of seconds, but was given {}",
                                core::to_string(duration.type()));
                    return;
                }

                // A negative wait is a wait of nothing rather than a fault: it can
                // only come of arithmetic, and stopping a chapter over it helps
                // nobody.
                this->pending_wait = std::max(0.0, *seconds);
                this->state = YieldStatus::WAITING_FOR_TIME;
                return;
            }

            case OpCode::MENU:
                this->execute_menu(this->chunk.read_operand(address, 0), address);
                return;

            case OpCode::SCENE:
            case OpCode::SHOW:
            case OpCode::HIDE:
                this->execute_presentation(instruction, this->chunk.read_operand(address, 0), address);
                return;

            case OpCode::HALT:
                this->state = YieldStatus::FINISHED;
                return;
        }
    }

    void VirtualMachine::execute_arithmetic(const OpCode instruction, const std::uint32_t address)
    {
        if (instruction == OpCode::NEGATE)
        {
            if (!this->require(1, address))
            {
                return;
            }

            const core::Value value = this->pop();
            if (const std::optional<std::int64_t> integer = value.as_integer())
            {
                this->values.emplace_back(-*integer);
            }
            else if (const std::optional<double> floating = value.as_floating())
            {
                this->values.emplace_back(-*floating);
            }
            else
            {
                this->raise(address, "'-' needs a number, but was given {}",
                            core::to_string(value.type()));
            }

            return;
        }

        if (!this->require(2, address))
        {
            return;
        }

        const core::Value right = this->pop();
        const core::Value left = this->pop();

        if (instruction == OpCode::ADD && left.is_text() && right.is_text())
        {
            this->values.emplace_back(std::string(left.as_text()).append(right.as_text()));
            return;
        }

        if (!left.is_number() || !right.is_number())
        {
            this->raise(address, "'{}' needs two numbers, but was given {} and {}",
                        operator_spelling(instruction),
                        core::to_string(left.type()), core::to_string(right.type()));
            return;
        }

        // Integer with integer stays integer, which is the promise that `1 / 2` is
        // 0; anything mixed promotes, which is the promise that `1.0 / 2` is 0.5.
        if (left.is_integer() && right.is_integer())
        {
            const std::int64_t first = *left.as_integer();
            const std::int64_t second = *right.as_integer();

            if (instruction == OpCode::DIVIDE || instruction == OpCode::REMAINDER)
            {
                if (second == 0)
                {
                    this->raise(address, "division by zero");
                    return;
                }

                // The one pair of integers whose quotient is not representable.
                if (first == std::numeric_limits<std::int64_t>::min() && second == -1)
                {
                    this->raise(address, "the result of this division is too large for an integer");
                    return;
                }
            }

            switch (instruction)
            {
                case OpCode::ADD:       this->values.emplace_back(first + second); return;
                case OpCode::SUBTRACT:  this->values.emplace_back(first - second); return;
                case OpCode::MULTIPLY:  this->values.emplace_back(first * second); return;
                case OpCode::DIVIDE:    this->values.emplace_back(first / second); return;
                case OpCode::REMAINDER: this->values.emplace_back(first % second); return;
                default:                break;
            }

            return;
        }

        const double first = *left.to_floating();
        const double second = *right.to_floating();

        if ((instruction == OpCode::DIVIDE || instruction == OpCode::REMAINDER) && second == 0.0)
        {
            // Refused rather than allowed to produce an infinity. A story that
            // divides by zero has a mistake in it, and a number that prints as
            // "inf" halfway through a line is a worse way to learn about it.
            this->raise(address, "division by zero");
            return;
        }

        switch (instruction)
        {
            case OpCode::ADD:       this->values.emplace_back(first + second); return;
            case OpCode::SUBTRACT:  this->values.emplace_back(first - second); return;
            case OpCode::MULTIPLY:  this->values.emplace_back(first * second); return;
            case OpCode::DIVIDE:    this->values.emplace_back(first / second); return;
            case OpCode::REMAINDER: this->values.emplace_back(std::fmod(first, second)); return;
            default:                break;
        }
    }

    void VirtualMachine::execute_comparison(const OpCode instruction, const std::uint32_t address)
    {
        if (!this->require(2, address))
        {
            return;
        }

        const core::Value right = this->pop();
        const core::Value left = this->pop();

        if (instruction == OpCode::EQUAL || instruction == OpCode::NOT_EQUAL)
        {
            // The language's equality promotes across the numeric types, so that
            // `1 == 1.0` holds. Value's own operator== is strict about type and is
            // what everything else in the engine compares with; this is the one
            // place that is deliberately looser.
            bool equal = false;
            if (left.is_number() && right.is_number())
            {
                equal = left.is_integer() && right.is_integer()
                            ? *left.as_integer() == *right.as_integer()
                            : *left.to_floating() == *right.to_floating();
            }
            else
            {
                equal = left == right;
            }

            this->values.emplace_back(instruction == OpCode::EQUAL ? equal : !equal);
            return;
        }

        if (!left.is_number() || !right.is_number())
        {
            // Text is deliberately unordered. Comparing it would have to answer
            // whether "Ё" comes before "Я", and this engine carries no collation
            // to answer with; a byte comparison would give an order that looks
            // like an answer and is wrong in every language with an alphabet.
            this->raise(address, "'{}' needs two numbers, but was given {} and {}",
                        operator_spelling(instruction),
                        core::to_string(left.type()), core::to_string(right.type()));
            return;
        }

        const bool exact = left.is_integer() && right.is_integer();
        const std::int64_t first_integer = exact ? *left.as_integer() : 0;
        const std::int64_t second_integer = exact ? *right.as_integer() : 0;
        const double first = exact ? 0.0 : *left.to_floating();
        const double second = exact ? 0.0 : *right.to_floating();

        bool result = false;
        switch (instruction)
        {
            case OpCode::LESS:
                result = exact ? first_integer < second_integer : first < second;
                break;
            case OpCode::LESS_OR_EQUAL:
                result = exact ? first_integer <= second_integer : first <= second;
                break;
            case OpCode::GREATER:
                result = exact ? first_integer > second_integer : first > second;
                break;
            case OpCode::GREATER_OR_EQUAL:
                result = exact ? first_integer >= second_integer : first >= second;
                break;
            default:
                break;
        }

        this->values.emplace_back(result);
    }

    void VirtualMachine::execute_concatenate(const std::uint32_t count, const std::uint32_t address)
    {
        if (!this->require(count, address))
        {
            return;
        }

        const std::size_t first = this->values.size() - count;

        std::string joined;
        for (std::size_t index = first; index < this->values.size(); ++index)
        {
            joined += to_display_text(this->values[index]);
        }

        this->values.resize(first);
        this->values.emplace_back(std::move(joined));
    }

    void VirtualMachine::execute_say(const std::uint32_t speaker, const std::uint32_t address)
    {
        if (!this->require(1, address))
        {
            return;
        }

        SayCommand command{.text = to_display_text(this->pop())};

        if (speaker < this->chunk.constants().size())
        {
            const core::Value& who = this->chunk.constants()[speaker];
            if (who.is_text())
            {
                command.speaker = std::string(who.as_text());
            }
        }

        this->sink.say(command);
        this->state = YieldStatus::WAITING_FOR_ADVANCE;
    }

    void VirtualMachine::execute_menu(const std::uint32_t table, const std::uint32_t address)
    {
        if (table >= this->chunk.menu_tables().size())
        {
            this->raise(address, "there is no menu {}", table);
            return;
        }

        const std::size_t count = this->chunk.menu_tables()[table].targets.size();
        if (count == 0)
        {
            this->raise(address, "a menu with no choices cannot be offered");
            return;
        }

        if (!this->require(count, address))
        {
            return;
        }

        const std::size_t first = this->values.size() - count;

        MenuCommand command;
        command.prompts.reserve(count);
        for (std::size_t index = first; index < this->values.size(); ++index)
        {
            command.prompts.push_back(to_display_text(this->values[index]));
        }

        this->values.resize(first);

        this->sink.offer(command);
        this->offered_menu = table;
        this->state = YieldStatus::WAITING_FOR_CHOICE;
    }

    void VirtualMachine::execute_presentation(const OpCode instruction, const std::uint32_t index,
                                              const std::uint32_t address)
    {
        if (index >= this->chunk.commands().size())
        {
            this->raise(address, "there is no presentation command {}", index);
            return;
        }

        const PresentationRecord& record = this->chunk.commands()[index];

        if (instruction == OpCode::SCENE)
        {
            this->sink.scene(SceneCommand{
                .background = record.target,
                .transition = record.transition,
            });
            return;
        }

        if (instruction == OpCode::HIDE)
        {
            this->sink.hide(HideCommand{.name = record.target, .transition = record.transition});
            return;
        }

        ShowCommand command{
            .asset = record.target,
            .anchor = record.anchor,
            .transition = record.transition,
        };

        if (record.position_on_stack)
        {
            if (!this->require(2, address))
            {
                return;
            }

            // Pushed in the order written, so the vertical coordinate is on top.
            const core::Value vertical = this->pop();
            const core::Value horizontal = this->pop();

            const std::optional<double> down = vertical.to_floating();
            const std::optional<double> across = horizontal.to_floating();

            if (!down.has_value() || !across.has_value())
            {
                this->raise(address, "a position needs two numbers, but was given {} and {}",
                            core::to_string(horizontal.type()), core::to_string(vertical.type()));
                return;
            }

            command.position = ScreenPosition{.x = *across, .y = *down};
        }

        // None of the three suspends: a transition runs on screen while the story
        // goes on, which is what a novel does and what an author expects.
        this->sink.show(command);
    }

    void VirtualMachine::execute_call(const std::uint32_t target, const std::uint32_t address)
    {
        if (this->frames.size() >= MAXIMUM_CALL_DEPTH)
        {
            this->raise(address,
                        "calls are nested more than {} deep, which is a script calling itself "
                        "without an end to it",
                        MAXIMUM_CALL_DEPTH);
            return;
        }

        this->frames.push_back(CallFrame{
            .return_address = this->ip,
            .stack_base = static_cast<std::uint32_t>(this->values.size()),
        });

        this->ip = target;
    }

    void VirtualMachine::execute_return()
    {
        // A return with nothing to return to is the end of the script rather than
        // a fault: it is what the outermost routine does when it runs out.
        if (this->frames.empty())
        {
            this->state = YieldStatus::FINISHED;
            return;
        }

        const CallFrame frame = this->frames.back();
        this->frames.pop_back();

        // Anything the routine left behind goes with it, so a call cannot alter the
        // stack its caller was working on.
        if (this->values.size() > frame.stack_base)
        {
            this->values.resize(frame.stack_base);
        }

        this->ip = frame.return_address;
    }
}
