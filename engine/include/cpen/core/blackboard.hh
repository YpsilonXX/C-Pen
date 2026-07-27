#ifndef CPEN_CORE_BLACKBOARD_HH
#define CPEN_CORE_BLACKBOARD_HH

#include "cpen/core/value.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpen::core
{
    class EventBus;

    /// Handle to one blackboard slot.
    ///
    /// Runtime-only and never serialised: a save file stores variable *names*, and
    /// identifiers are resolved again on load. Compiled script chunks likewise
    /// carry a table of global names and bind them to identifiers once, when the
    /// chunk is loaded, which is what keeps a chunk independent of the blackboard
    /// instance that will eventually run it.
    enum class SymbolId : std::uint32_t
    {
    };

    inline constexpr SymbolId INVALID_SYMBOL{std::numeric_limits<std::uint32_t>::max()};

    /// Emitted on the event bus when a *watched* slot takes a new value.
    /// Unwatched slots are silent, so ordinary script assignment costs nothing.
    struct VariableChanged
    {
        SymbolId symbol = INVALID_SYMBOL;
        Value previous{};
        Value current{};
    };

    /// The shared variable store: script globals, story flags and — later —
    /// inventory all live here, so a mini-game or a C++ behaviour reads exactly
    /// what the VM wrote, with no copy to keep in sync.
    ///
    /// Names are interned once and addressed by SymbolId afterwards. The VM's
    /// global access is an index into a dense vector rather than a hash lookup per
    /// instruction, while C++ call sites and tests keep the convenience of
    /// addressing a variable by name.
    ///
    /// Not thread-safe: it belongs to the deterministic main-thread loop.
    class Blackboard
    {
    public:
        Blackboard() = default;

        /// Attaches an event bus for VariableChanged notifications. Without one,
        /// watching still records intent but nothing is published.
        explicit Blackboard(EventBus& event_bus) noexcept
            : bus(&event_bus)
        {
        }

        void set_event_bus(EventBus* event_bus) noexcept
        {
            this->bus = event_bus;
        }

        /// Returns the identifier for `name`, creating a NIL slot if it is new.
        /// Stable for the lifetime of this blackboard, or until clear().
        SymbolId intern(std::string_view name);

        /// Look-up without creating a slot.
        std::optional<SymbolId> find(std::string_view name) const;

        /// Empty view for an unknown identifier.
        std::string_view name_of(SymbolId symbol) const;

        /// Reading an unknown name or a stale identifier yields NIL rather than
        /// failing: "not assigned yet" and "assigned nil" are the same state to
        /// the store. Whether the *language* tolerates reading an undefined
        /// variable is a separate decision, enforced by the compiler.
        const Value& get(SymbolId symbol) const;
        const Value& get(std::string_view name) const;

        /// Hot path used by the VM. A stale identifier is reported and ignored.
        void set(SymbolId symbol, Value value);

        /// Interns `name` if it is new.
        void set(std::string_view name, Value value);

        /// Marks a slot as observed, so writes that actually change it publish
        /// VariableChanged. Watching by name interns it, which lets a listener
        /// register before any script has assigned the variable.
        void watch(SymbolId symbol);
        SymbolId watch(std::string_view name);
        void unwatch(SymbolId symbol);
        bool is_watched(SymbolId symbol) const;

        std::size_t size() const noexcept
        {
            return this->slots.size();
        }

        /// Resets every slot to NIL while keeping names and identifiers valid.
        /// This is the "start a new game / apply a loaded save" operation: already
        /// bound chunks and cached identifiers stay usable.
        void reset_values();

        /// Drops names and values alike. Every previously issued SymbolId becomes
        /// stale, so this is for teardown, not for loading a save.
        void clear();

        /// Visits every slot in identifier order as (SymbolId, name, value).
        /// Used by the serialiser and by diagnostics.
        template <typename Visitor>
        void for_each(Visitor&& visitor) const
        {
            for (std::size_t index = 0; index < this->slots.size(); ++index)
            {
                const Slot& slot = this->slots[index];
                visitor(static_cast<SymbolId>(static_cast<std::uint32_t>(index)),
                        slot.name,
                        slot.value);
            }
        }

    private:
        struct Slot
        {
            /// Points into the owning key of `symbols`. unordered_map guarantees
            /// element references stay valid across rehashing, so the view remains
            /// good for as long as the entry lives; storing a second std::string
            /// per slot would be the only alternative.
            std::string_view name;
            Value value{};
            bool watched = false;
        };

        /// Transparent hashing lets a std::string_view key be looked up without
        /// materialising a std::string first.
        struct StringHash
        {
            using is_transparent = void;

            std::size_t operator()(const std::string_view text) const noexcept
            {
                return std::hash<std::string_view>{}(text);
            }
        };

        static const Value& nil();
        static std::size_t index_of(SymbolId symbol) noexcept
        {
            return static_cast<std::size_t>(static_cast<std::uint32_t>(symbol));
        }

        std::unordered_map<std::string, SymbolId, StringHash, std::equal_to<>> symbols;
        std::vector<Slot> slots;
        EventBus* bus = nullptr;
    };
}

#endif //CPEN_CORE_BLACKBOARD_HH
