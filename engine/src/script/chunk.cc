#include "cpen/script/chunk.hh"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>
#include <utility>

namespace cpen::script
{
    namespace
    {
        constexpr std::array<std::string_view, OPCODE_COUNT> OPCODE_NAMES = {
#define CPEN_OPCODE(name, first, second, description) #name,
#include "cpen/script/opcodes.def"
        };

        constexpr std::array<std::string_view, OPCODE_COUNT> OPCODE_DESCRIPTIONS = {
#define CPEN_OPCODE(name, first, second, description) description,
#include "cpen/script/opcodes.def"
        };

        struct OperandPair
        {
            OperandKind first;
            OperandKind second;
        };

        constexpr auto OPCODE_OPERANDS = std::to_array<OperandPair>({
#define CPEN_OPCODE(name, first, second, description) {OperandKind::first, OperandKind::second},
#include "cpen/script/opcodes.def"
        });

        constexpr std::size_t OPERAND_SIZE = 4;

        std::size_t index_of(const OpCode instruction) noexcept
        {
            const auto index = static_cast<std::size_t>(instruction);
            return index < OPCODE_COUNT ? index : 0;
        }

        /// Little-endian on purpose rather than by copying the machine's own byte
        /// order: the day a chunk is written to a file or sent to another process,
        /// the format is already defined and does not have to be renegotiated.
        std::uint32_t read_word(const std::span<const std::byte> code, const std::size_t offset)
        {
            if (offset + OPERAND_SIZE > code.size())
            {
                throw std::out_of_range("script: operand read past the end of the code");
            }

            return std::to_integer<std::uint32_t>(code[offset])
                | (std::to_integer<std::uint32_t>(code[offset + 1]) << 8)
                | (std::to_integer<std::uint32_t>(code[offset + 2]) << 16)
                | (std::to_integer<std::uint32_t>(code[offset + 3]) << 24);
        }
    }

    std::string_view to_string(const OpCode instruction) noexcept
    {
        return OPCODE_NAMES[index_of(instruction)];
    }

    std::string_view describe(const OpCode instruction) noexcept
    {
        return OPCODE_DESCRIPTIONS[index_of(instruction)];
    }

    OperandKind operand_kind(const OpCode instruction, const std::size_t index) noexcept
    {
        const OperandPair& operands = OPCODE_OPERANDS[index_of(instruction)];

        switch (index)
        {
            case 0:  return operands.first;
            case 1:  return operands.second;
            default: return OperandKind::NONE;
        }
    }

    std::size_t operand_count(const OpCode instruction) noexcept
    {
        const OperandPair& operands = OPCODE_OPERANDS[index_of(instruction)];

        if (operands.first == OperandKind::NONE)
        {
            return 0;
        }

        return operands.second == OperandKind::NONE ? 1 : 2;
    }

    std::size_t instruction_size(const OpCode instruction) noexcept
    {
        return 1 + operand_count(instruction) * OPERAND_SIZE;
    }

    std::optional<std::uint32_t> Chunk::find_label(const std::string_view name) const
    {
        const auto found = this->label_addresses.find(std::string(name));
        return found != this->label_addresses.end() ? std::optional(found->second) : std::nullopt;
    }

    OpCode Chunk::read_opcode(const std::uint32_t offset) const
    {
        if (offset >= this->instructions.size())
        {
            throw std::out_of_range("script: instruction read past the end of the code");
        }

        return static_cast<OpCode>(std::to_integer<std::uint8_t>(this->instructions[offset]));
    }

    std::uint32_t Chunk::read_operand(const std::uint32_t offset, const std::size_t index) const
    {
        if (index >= operand_count(this->read_opcode(offset)))
        {
            throw std::out_of_range("script: instruction has no operand of that index");
        }

        return read_word(this->instructions, offset + 1 + index * OPERAND_SIZE);
    }

    SourceSpan Chunk::span_at(const std::uint32_t offset) const noexcept
    {
        // The entries are appended in increasing offset order, so the instruction
        // covering this offset is the last one that does not begin after it.
        const auto after = std::ranges::upper_bound(this->spans, offset, {},
                                                    &InstructionSpan::offset);
        if (after == this->spans.begin())
        {
            return SourceSpan{};
        }

        return std::prev(after)->span;
    }

    void Chunk::write_word(const std::uint32_t value)
    {
        this->instructions.push_back(static_cast<std::byte>(value & 0xFF));
        this->instructions.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
        this->instructions.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
        this->instructions.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
    }

    std::uint32_t Chunk::emit(const OpCode instruction, const SourceSpan span)
    {
        const auto offset = static_cast<std::uint32_t>(this->instructions.size());

        this->instructions.push_back(static_cast<std::byte>(instruction));
        this->spans.push_back(InstructionSpan{.offset = offset, .span = span});

        return offset;
    }

    std::uint32_t Chunk::emit(const OpCode instruction, const std::uint32_t first,
                              const SourceSpan span)
    {
        const std::uint32_t offset = this->emit(instruction, span);
        this->write_word(first);

        return offset;
    }

    std::uint32_t Chunk::emit(const OpCode instruction, const std::uint32_t first,
                              const std::uint32_t second, const SourceSpan span)
    {
        const std::uint32_t offset = this->emit(instruction, first, span);
        this->write_word(second);

        return offset;
    }

    void Chunk::patch_address(const std::uint32_t instruction_offset, const std::uint32_t target)
    {
        if (operand_count(this->read_opcode(instruction_offset)) == 0)
        {
            throw std::out_of_range("script: the instruction being patched takes no operand");
        }

        const std::size_t at = instruction_offset + 1;
        if (at + OPERAND_SIZE > this->instructions.size())
        {
            throw std::out_of_range("script: patched operand lies past the end of the code");
        }

        this->instructions[at] = static_cast<std::byte>(target & 0xFF);
        this->instructions[at + 1] = static_cast<std::byte>((target >> 8) & 0xFF);
        this->instructions[at + 2] = static_cast<std::byte>((target >> 16) & 0xFF);
        this->instructions[at + 3] = static_cast<std::byte>((target >> 24) & 0xFF);
    }

    std::uint32_t Chunk::add_constant(core::Value value)
    {
        // Linear, and deliberately so: the constants of one script are counted in
        // dozens, and a hash of a variant value would cost more to write and keep
        // correct than the search it saves.
        const auto found = std::ranges::find(this->constant_pool, value);
        if (found != this->constant_pool.end())
        {
            return static_cast<std::uint32_t>(std::distance(this->constant_pool.begin(), found));
        }

        this->constant_pool.push_back(std::move(value));
        return static_cast<std::uint32_t>(this->constant_pool.size() - 1);
    }

    std::uint32_t Chunk::add_global(const std::string_view name)
    {
        const auto found = std::ranges::find(this->global_names, name);
        if (found != this->global_names.end())
        {
            return static_cast<std::uint32_t>(std::distance(this->global_names.begin(), found));
        }

        this->global_names.emplace_back(name);
        return static_cast<std::uint32_t>(this->global_names.size() - 1);
    }

    std::uint32_t Chunk::add_menu_table(MenuTable table)
    {
        this->menus.push_back(std::move(table));
        return static_cast<std::uint32_t>(this->menus.size() - 1);
    }

    void Chunk::set_menu_table(const std::uint32_t index, MenuTable table)
    {
        if (index >= this->menus.size())
        {
            throw std::out_of_range("script: no such menu table");
        }

        this->menus[index] = std::move(table);
    }

    std::uint32_t Chunk::add_command(PresentationRecord record)
    {
        this->records.push_back(std::move(record));
        return static_cast<std::uint32_t>(this->records.size() - 1);
    }

    bool Chunk::add_label(std::string name, const std::uint32_t address)
    {
        return this->label_addresses.emplace(std::move(name), address).second;
    }

    std::string disassemble_instruction(const Chunk& chunk, const std::uint32_t offset)
    {
        const OpCode instruction = chunk.read_opcode(offset);

        std::string operands;
        for (std::size_t index = 0; index < operand_count(instruction); ++index)
        {
            if (!operands.empty())
            {
                operands += "  ";
            }

            const std::uint32_t value = chunk.read_operand(offset, index);

            switch (operand_kind(instruction, index))
            {
                case OperandKind::CONSTANT:
                    operands += std::format("{}  ; {}", value,
                                            value < chunk.constants().size()
                                                ? chunk.constants()[value].to_string()
                                                : "<no such constant>");
                    break;

                case OperandKind::GLOBAL:
                    operands += std::format("{}  ; {}", value,
                                            value < chunk.globals().size()
                                                ? chunk.globals()[value]
                                                : "<no such name>");
                    break;

                case OperandKind::ADDRESS:
                    operands += std::format("-> {:04x}", value);
                    break;

                case OperandKind::COUNT:
                    operands += std::format("{}", value);
                    break;

                case OperandKind::MENU_TABLE:
                {
                    operands += std::format("{}  ;", value);

                    if (value >= chunk.menu_tables().size())
                    {
                        operands += " <no such table>";
                        break;
                    }

                    for (const std::uint32_t target : chunk.menu_tables()[value].targets)
                    {
                        operands += std::format(" -> {:04x}", target);
                    }

                    break;
                }

                case OperandKind::COMMAND:
                {
                    operands += std::format("{}  ;", value);

                    if (value >= chunk.commands().size())
                    {
                        operands += " <no such command>";
                        break;
                    }

                    const PresentationRecord& record = chunk.commands()[value];
                    operands += std::format(" {}", record.target);

                    if (!record.anchor.empty())
                    {
                        operands += std::format(" at {}", record.anchor);
                    }
                    else if (record.position_on_stack)
                    {
                        operands += " at the position on the stack";
                    }

                    if (!record.transition.empty())
                    {
                        operands += std::format(" with {}", record.transition);
                    }

                    break;
                }

                case OperandKind::NONE:
                    break;
            }
        }

        if (operands.empty())
        {
            return std::format("{:04x} {}", offset, to_string(instruction));
        }

        return std::format("{:04x} {:<18} {}", offset, to_string(instruction), operands);
    }

    std::string disassemble(const Chunk& chunk)
    {
        std::string listing;

        std::uint32_t offset = 0;
        while (offset < chunk.size())
        {
            listing += disassemble_instruction(chunk, offset);
            listing.push_back('\n');

            offset += static_cast<std::uint32_t>(instruction_size(chunk.read_opcode(offset)));
        }

        return listing;
    }
}
