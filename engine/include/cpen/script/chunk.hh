#ifndef CPEN_SCRIPT_CHUNK_HH
#define CPEN_SCRIPT_CHUNK_HH

#include "cpen/core/value.hh"
#include "cpen/script/source_span.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cpen::script
{
    /// What an operand refers to, which is all the disassembler needs to know to
    /// render any instruction — including one added after it was written.
    enum class OperandKind : std::uint8_t
    {
        NONE,

        /// An index into the chunk's constants.
        CONSTANT,

        /// An index into the chunk's global names.
        GLOBAL,

        /// An absolute offset into the code.
        ADDRESS,

        /// A plain number, meaning whatever the instruction says it means.
        COUNT,

        /// An index into the chunk's menu tables.
        MENU_TABLE,
    };

    enum class OpCode : std::uint8_t
    {
#define CPEN_OPCODE(name, first, second, description) name,
#include "cpen/script/opcodes.def"
    };

    inline constexpr std::size_t OPCODE_COUNT = 0
#define CPEN_OPCODE(name, first, second, description) + 1
#include "cpen/script/opcodes.def"
        ;

    /// The enumerator's own name, for a disassembly.
    std::string_view to_string(OpCode instruction) noexcept;

    /// The one-line explanation from the table, for tooling and for messages.
    std::string_view describe(OpCode instruction) noexcept;

    /// The kind of operand `index` — NONE past the end, so a caller can walk to
    /// two without asking how many there are.
    OperandKind operand_kind(OpCode instruction, std::size_t index) noexcept;

    std::size_t operand_count(OpCode instruction) noexcept;

    /// Bytes one whole instruction occupies: the opcode and its operands.
    std::size_t instruction_size(OpCode instruction) noexcept;

    /// Where each choice of a menu continues.
    ///
    /// The addresses live here rather than in the instruction stream because a
    /// menu has as many of them as it has choices, and an instruction of variable
    /// length would cost every other instruction the ability to be skipped
    /// without being understood.
    struct MenuTable
    {
        std::vector<std::uint32_t> targets{};
    };

    /// A compiled script: instructions and everything they refer to.
    ///
    /// Static once compiled — every changing part of a running script (the
    /// instruction pointer, the value stack, the variables) lives in the machine
    /// instead. That split is what makes saving a game a matter of writing down
    /// the machine's state and nothing else, and what lets one chunk be run by
    /// two machines at once.
    ///
    /// The building half of this interface belongs to the compiler. Nothing else
    /// should call it: the machine and the disassembler take a chunk by const
    /// reference and only read.
    class Chunk
    {
    public:
        std::span<const std::byte> code() const noexcept { return this->instructions; }

        std::uint32_t size() const noexcept
        {
            return static_cast<std::uint32_t>(this->instructions.size());
        }

        const std::vector<core::Value>& constants() const noexcept { return this->constant_pool; }
        const std::vector<std::string>& globals() const noexcept { return this->global_names; }
        const std::vector<MenuTable>& menu_tables() const noexcept { return this->menus; }
        const std::unordered_map<std::string, std::uint32_t>& labels() const noexcept
        {
            return this->label_addresses;
        }

        std::optional<std::uint32_t> find_label(std::string_view name) const;

        /// The opcode of the instruction beginning at `offset`.
        OpCode read_opcode(std::uint32_t offset) const;

        /// Operand `index` of the instruction beginning at `offset`.
        std::uint32_t read_operand(std::uint32_t offset, std::size_t index) const;

        /// Where in the script the instruction at `offset` came from, for a
        /// run-time diagnostic that has to name a line. Carried per instruction
        /// from the first one emitted: a machine that can only say "a value was
        /// nil" and not where is a machine nobody can debug.
        SourceSpan span_at(std::uint32_t offset) const noexcept;

        // --- building, for the compiler ------------------------------------

        /// Appends an instruction and returns the offset it begins at, which is
        /// what a forward jump is patched by.
        std::uint32_t emit(OpCode instruction, SourceSpan span);
        std::uint32_t emit(OpCode instruction, std::uint32_t first, SourceSpan span);
        std::uint32_t emit(OpCode instruction, std::uint32_t first, std::uint32_t second,
                           SourceSpan span);

        /// Fills in the address operand of a jump emitted earlier.
        void patch_address(std::uint32_t instruction_offset, std::uint32_t target);

        /// Interns a constant, so that a value repeated in a script is stored
        /// once. Values compare by type as well as content, so 1 and 1.0 remain
        /// two constants.
        std::uint32_t add_constant(core::Value value);

        std::uint32_t add_global(std::string_view name);
        std::uint32_t add_menu_table(MenuTable table);

        /// Records where a label begins. Returns false if the name is already
        /// taken, which is the caller's cue to report a duplicate.
        bool add_label(std::string name, std::uint32_t address);

    private:
        void write_word(std::uint32_t value);

        struct InstructionSpan
        {
            std::uint32_t offset = 0;
            SourceSpan span{};
        };

        std::vector<std::byte> instructions{};
        std::vector<core::Value> constant_pool{};
        std::vector<std::string> global_names{};
        std::vector<MenuTable> menus{};
        std::unordered_map<std::string, std::uint32_t> label_addresses{};

        /// One entry per instruction, in increasing offset order, so a lookup is
        /// a binary search and the common case — never asking — costs nothing.
        std::vector<InstructionSpan> spans{};
    };

    /// Renders the chunk as text, one instruction per line.
    ///
    ///     0000 PUSH_CONSTANT   0        ; 1
    ///     0005 LOAD_GLOBAL     0        ; sympathy
    ///     000a JUMP_IF_FALSE   -> 0019
    ///
    /// Both a debugging tool and the third golden dump: bytecode compared by hand
    /// is bytecode nobody checks, and a compiler whose output can only be
    /// inspected by running it is a compiler that gets changed by guesswork.
    std::string disassemble(const Chunk& chunk);

    /// One instruction, without the surrounding lines. Used by the disassembler
    /// and by a machine trace.
    std::string disassemble_instruction(const Chunk& chunk, std::uint32_t offset);
}

#endif //CPEN_SCRIPT_CHUNK_HH
