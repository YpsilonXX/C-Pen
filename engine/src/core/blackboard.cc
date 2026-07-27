#include "cpen/core/blackboard.hh"

#include "cpen/core/event_bus.hh"
#include "cpen/core/log.hh"

namespace cpen::core
{
    const Value& Blackboard::nil()
    {
        static const Value value{};
        return value;
    }

    SymbolId Blackboard::intern(const std::string_view name)
    {
        if (const auto existing = this->symbols.find(name); existing != this->symbols.end())
        {
            return existing->second;
        }

        const auto symbol = static_cast<SymbolId>(static_cast<std::uint32_t>(this->slots.size()));

        // The map entry owns the name; the slot only views it. Insert first so
        // that the view refers to storage that already exists.
        const auto [entry, inserted] = this->symbols.emplace(std::string(name), symbol);
        this->slots.push_back(Slot{.name = entry->first, .value = Value{}, .watched = false});

        log::trace(log::Category::CORE, "blackboard interned '{}' as symbol {}",
                   name, static_cast<std::uint32_t>(symbol));
        return symbol;
    }

    std::optional<SymbolId> Blackboard::find(const std::string_view name) const
    {
        const auto entry = this->symbols.find(name);
        if (entry == this->symbols.end())
        {
            return std::nullopt;
        }
        return entry->second;
    }

    std::string_view Blackboard::name_of(const SymbolId symbol) const
    {
        const std::size_t index = index_of(symbol);
        if (index >= this->slots.size())
        {
            return {};
        }
        return this->slots[index].name;
    }

    const Value& Blackboard::get(const SymbolId symbol) const
    {
        const std::size_t index = index_of(symbol);
        if (index >= this->slots.size())
        {
            return nil();
        }
        return this->slots[index].value;
    }

    const Value& Blackboard::get(const std::string_view name) const
    {
        const auto entry = this->symbols.find(name);
        if (entry == this->symbols.end())
        {
            return nil();
        }
        return this->slots[index_of(entry->second)].value;
    }

    void Blackboard::set(const SymbolId symbol, Value value)
    {
        const std::size_t index = index_of(symbol);
        if (index >= this->slots.size())
        {
            log::error(log::Category::CORE, "blackboard write to unknown symbol {}",
                       static_cast<std::uint32_t>(symbol));
            return;
        }

        Slot& slot = this->slots[index];
        if (!slot.watched || this->bus == nullptr)
        {
            slot.value = std::move(value);
            return;
        }

        // A write that changes nothing is not a change: handlers observing a
        // variable should not fire on `$ x = x`.
        if (slot.value == value)
        {
            return;
        }

        Value previous = std::exchange(slot.value, std::move(value));
        this->bus->publish(VariableChanged{
            .symbol = symbol,
            .previous = std::move(previous),
            .current = slot.value,
        });
    }

    void Blackboard::set(const std::string_view name, Value value)
    {
        this->set(this->intern(name), std::move(value));
    }

    void Blackboard::watch(const SymbolId symbol)
    {
        const std::size_t index = index_of(symbol);
        if (index >= this->slots.size())
        {
            log::error(log::Category::CORE, "blackboard watch on unknown symbol {}",
                       static_cast<std::uint32_t>(symbol));
            return;
        }
        this->slots[index].watched = true;
    }

    SymbolId Blackboard::watch(const std::string_view name)
    {
        const SymbolId symbol = this->intern(name);
        this->slots[index_of(symbol)].watched = true;
        return symbol;
    }

    void Blackboard::unwatch(const SymbolId symbol)
    {
        const std::size_t index = index_of(symbol);
        if (index < this->slots.size())
        {
            this->slots[index].watched = false;
        }
    }

    bool Blackboard::is_watched(const SymbolId symbol) const
    {
        const std::size_t index = index_of(symbol);
        return index < this->slots.size() && this->slots[index].watched;
    }

    void Blackboard::reset_values()
    {
        for (Slot& slot : this->slots)
        {
            slot.value = Value{};
        }
    }

    void Blackboard::clear()
    {
        this->slots.clear();
        this->symbols.clear();
    }
}
