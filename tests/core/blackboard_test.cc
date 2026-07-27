#include <catch2/catch_test_macros.hpp>

#include "cpen/core/blackboard.hh"
#include "cpen/core/event_bus.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using cpen::core::Blackboard;
using cpen::core::EventBus;
using cpen::core::SymbolId;
using cpen::core::Value;
using cpen::core::VariableChanged;
using cpen::test::trace;

namespace
{
    std::uint32_t raw(const SymbolId symbol)
    {
        return static_cast<std::uint32_t>(symbol);
    }
}

TEST_CASE("interning is idempotent and hands out dense identifiers", "[core][blackboard]")
{
    Blackboard blackboard;

    const SymbolId affection = blackboard.intern("симпатия");
    const SymbolId chapter = blackboard.intern("chapter");
    const SymbolId again = blackboard.intern("симпатия");

    trace("interned 'симпатия'={}, 'chapter'={}, re-interned 'симпатия'={}", raw(affection),
          raw(chapter), raw(again));

    CHECK(affection == again);
    CHECK(raw(affection) == 0);
    CHECK(raw(chapter) == 1);
    CHECK(blackboard.size() == 2);
    CHECK(blackboard.name_of(affection) == "симпатия");
}

TEST_CASE("find never creates a slot", "[core][blackboard]")
{
    Blackboard blackboard;
    blackboard.intern("known");

    trace("find('known') has value={}, find('unknown') has value={}, size={}",
          blackboard.find("known").has_value(), blackboard.find("unknown").has_value(),
          blackboard.size());

    CHECK(blackboard.find("known").has_value());
    CHECK_FALSE(blackboard.find("unknown").has_value());
    CHECK(blackboard.size() == 1);
}

TEST_CASE("unknown names and stale identifiers read as nil", "[core][blackboard]")
{
    Blackboard blackboard;

    const Value& by_name = blackboard.get("never assigned");
    const Value& by_symbol = blackboard.get(static_cast<SymbolId>(9999));

    trace("get(\"never assigned\")={}, get(symbol 9999)={}, size stayed {}", by_name.to_string(),
          by_symbol.to_string(), blackboard.size());

    CHECK(by_name.is_nil());
    CHECK(by_symbol.is_nil());
    CHECK(blackboard.size() == 0);
}

TEST_CASE("name and identifier access address the same slot", "[core][blackboard]")
{
    Blackboard blackboard;

    blackboard.set("gold", 120);
    const SymbolId gold = blackboard.intern("gold");

    trace("set by name, read by symbol: {}", blackboard.get(gold).to_string());
    CHECK(blackboard.get(gold).as_integer() == 120);

    blackboard.set(gold, Value{"broke"});
    trace("set by symbol, read by name: {}", blackboard.get("gold").to_string());
    CHECK(blackboard.get("gold").as_text() == "broke");

    // Writing by name interns on demand, so the store never needs declaration.
    blackboard.set("new flag", true);
    trace("writing an unknown name created slot {}, size={}", raw(blackboard.intern("new flag")),
          blackboard.size());
    CHECK(blackboard.get("new flag").as_boolean() == true);
    CHECK(blackboard.size() == 2);
}

TEST_CASE("a watched slot publishes only when the value really changes", "[core][blackboard]")
{
    EventBus bus;
    Blackboard blackboard(bus);
    std::vector<VariableChanged> changes;

    const auto subscription = bus.subscribe<VariableChanged>(
        [&changes](const VariableChanged& event) { changes.push_back(event); });

    const SymbolId affection = blackboard.watch("симпатия");

    blackboard.set(affection, 1);
    bus.dispatch_pending();
    trace("nil -> 1 produced {} event(s)", changes.size());
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].symbol == affection);
    CHECK(changes[0].previous.is_nil());
    CHECK(changes[0].current.as_integer() == 1);

    blackboard.set(affection, 1);
    bus.dispatch_pending();
    trace("1 -> 1 (same value) produced {} event(s) in total", changes.size());
    CHECK(changes.size() == 1);

    blackboard.set(affection, 2);
    bus.dispatch_pending();
    trace("1 -> 2 produced {} event(s) in total, last: {} -> {}", changes.size(),
          changes.back().previous.to_string(), changes.back().current.to_string());
    REQUIRE(changes.size() == 2);
    CHECK(changes[1].previous.as_integer() == 1);
    CHECK(changes[1].current.as_integer() == 2);
}

TEST_CASE("unwatched slots stay silent", "[core][blackboard]")
{
    EventBus bus;
    Blackboard blackboard(bus);
    int events = 0;

    const auto subscription =
        bus.subscribe<VariableChanged>([&events](const VariableChanged&) { ++events; });

    blackboard.set("noisy candidate", 1);
    blackboard.set("noisy candidate", 2);
    bus.dispatch_pending();

    trace("two writes to an unwatched slot produced {} event(s), pending={}", events,
          bus.pending_count());
    CHECK(events == 0);

    const SymbolId watched = blackboard.watch("noisy candidate");
    blackboard.set(watched, 3);
    bus.dispatch_pending();
    trace("after watch(), the next write produced {} event(s)", events);
    CHECK(events == 1);

    blackboard.unwatch(watched);
    blackboard.set(watched, 4);
    bus.dispatch_pending();
    trace("after unwatch(), the write produced no further events: total={}", events);
    CHECK(events == 1);
    CHECK_FALSE(blackboard.is_watched(watched));
}

TEST_CASE("watching without a bus is recorded but silent", "[core][blackboard]")
{
    Blackboard blackboard;
    const SymbolId symbol = blackboard.watch("lonely");

    blackboard.set(symbol, 1);

    trace("no bus attached: is_watched={}, value={}", blackboard.is_watched(symbol),
          blackboard.get(symbol).to_string());
    CHECK(blackboard.is_watched(symbol));
    CHECK(blackboard.get(symbol).as_integer() == 1);
}

TEST_CASE("reset_values keeps identifiers, clear invalidates them", "[core][blackboard]")
{
    Blackboard blackboard;
    const SymbolId chapter = blackboard.intern("chapter");
    blackboard.set(chapter, 3);

    blackboard.reset_values();
    trace("after reset_values: size={}, name_of(chapter)='{}', value={}", blackboard.size(),
          blackboard.name_of(chapter), blackboard.get(chapter).to_string());
    CHECK(blackboard.size() == 1);
    CHECK(blackboard.name_of(chapter) == "chapter");
    CHECK(blackboard.get(chapter).is_nil());

    // The identifier survives, which is what lets a loaded save write straight
    // through the bindings a compiled chunk already resolved.
    blackboard.set(chapter, 7);
    CHECK(blackboard.get(chapter).as_integer() == 7);

    blackboard.clear();
    trace("after clear: size={}, find('chapter') has value={}", blackboard.size(),
          blackboard.find("chapter").has_value());
    CHECK(blackboard.size() == 0);
    CHECK_FALSE(blackboard.find("chapter").has_value());
}

TEST_CASE("for_each enumerates every slot in identifier order", "[core][blackboard]")
{
    Blackboard blackboard;
    blackboard.set("first", 1);
    blackboard.set("second", "two");
    blackboard.set("third", 3.5);

    std::vector<std::string> lines;
    blackboard.for_each(
        [&lines](const SymbolId symbol, const std::string_view name, const Value& value)
        {
            lines.push_back(std::format("{}:{}={}", raw(symbol), name, value.to_string()));
        });

    for (const std::string& line : lines)
    {
        trace("{}", line);
    }

    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "0:first=1");
    CHECK(lines[1] == "1:second=\"two\"");
    CHECK(lines[2] == "2:third=3.5");
}

TEST_CASE("writing through a stale identifier is reported and ignored", "[core][blackboard]")
{
    Blackboard blackboard;
    const cpen::test::LogCaptureGuard logs;

    blackboard.set(static_cast<SymbolId>(42), 1);

    trace("write to symbol 42 on an empty blackboard: logged={}, size={}",
          logs.contains("unknown symbol"), blackboard.size());
    CHECK(logs.contains("unknown symbol"));
    CHECK(blackboard.size() == 0);
}
