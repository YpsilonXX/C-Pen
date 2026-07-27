#include <catch2/catch_test_macros.hpp>

#include "cpen/core/event_bus.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <optional>
#include <string>
#include <typeindex>
#include <vector>

using cpen::core::EventBus;
using cpen::core::Subscription;
using cpen::test::trace;

namespace
{
    struct Ping
    {
        int value = 0;
    };

    struct Pong
    {
        std::string text;
    };
}

TEST_CASE("handlers do not run inside publish", "[core][event_bus]")
{
    EventBus bus;
    int received = 0;

    const auto subscription = bus.subscribe<Ping>([&received](const Ping&) { ++received; });

    bus.publish(Ping{.value = 1});
    trace("after publish: received={}, pending={}", received, bus.pending_count());
    CHECK(received == 0);
    CHECK(bus.pending_count() == 1);

    bus.dispatch_pending();
    trace("after dispatch_pending: received={}, pending={}", received, bus.pending_count());
    CHECK(received == 1);
    CHECK(bus.pending_count() == 0);
}

TEST_CASE("events are delivered in publish order", "[core][event_bus]")
{
    EventBus bus;
    std::vector<int> order;

    const auto subscription =
        bus.subscribe<Ping>([&order](const Ping& event) { order.push_back(event.value); });

    bus.publish(Ping{.value = 1});
    bus.publish(Ping{.value = 2});
    bus.publish(Ping{.value = 3});
    bus.dispatch_pending();

    trace("published 1,2,3 -> delivered {},{},{}", order[0], order[1], order[2]);
    CHECK(order == std::vector<int>{1, 2, 3});
}

TEST_CASE("handlers of one type never see another", "[core][event_bus]")
{
    EventBus bus;
    int pings = 0;
    std::string last_pong;

    const auto ping_subscription = bus.subscribe<Ping>([&pings](const Ping&) { ++pings; });
    const auto pong_subscription =
        bus.subscribe<Pong>([&last_pong](const Pong& event) { last_pong = event.text; });

    bus.publish(Pong{.text = "hello"});
    bus.dispatch_pending();

    trace("published one Pong: pings={}, last_pong=\"{}\"", pings, last_pong);
    CHECK(pings == 0);
    CHECK(last_pong == "hello");
    CHECK(bus.subscriber_count() == 2);
    CHECK(bus.subscriber_count(std::type_index(typeid(Ping))) == 1);
}

TEST_CASE("several handlers run in subscription order", "[core][event_bus]")
{
    EventBus bus;
    std::vector<std::string> calls;

    const auto first = bus.subscribe<Ping>([&calls](const Ping&) { calls.emplace_back("first"); });
    const auto second = bus.subscribe<Ping>([&calls](const Ping&) { calls.emplace_back("second"); });

    bus.publish(Ping{});
    bus.dispatch_pending();

    trace("handlers invoked: {}, {}", calls[0], calls[1]);
    CHECK(calls == std::vector<std::string>{"first", "second"});
}

TEST_CASE("dropping the token unsubscribes", "[core][event_bus]")
{
    EventBus bus;
    int received = 0;

    {
        const auto subscription = bus.subscribe<Ping>([&received](const Ping&) { ++received; });
        bus.publish(Ping{});
        bus.dispatch_pending();
        trace("inside scope: received={}, subscribers={}", received, bus.subscriber_count());
    }

    trace("after scope: subscribers={}", bus.subscriber_count());
    CHECK(bus.subscriber_count() == 0);

    bus.publish(Ping{});
    bus.dispatch_pending();
    trace("published again: received={}", received);
    CHECK(received == 1);
}

TEST_CASE("a handler unsubscribed during dispatch does not run", "[core][event_bus]")
{
    EventBus bus;
    bool second_ran = false;

    // Declared first so it outlives the handler that releases it.
    Subscription second;
    const auto first = bus.subscribe<Ping>([&second](const Ping&) { second.release(); });
    second = bus.subscribe<Ping>([&second_ran](const Ping&) { second_ran = true; });

    bus.publish(Ping{});
    bus.dispatch_pending();

    trace("first handler released the second before it ran: second_ran={}", second_ran);
    CHECK_FALSE(second_ran);
    CHECK(bus.subscriber_count() == 1);
}

TEST_CASE("a handler subscribing during dispatch waits for the next event", "[core][event_bus]")
{
    EventBus bus;
    int late_received = 0;
    std::optional<Subscription> late;

    const auto first = bus.subscribe<Ping>(
        [&bus, &late, &late_received](const Ping&)
        {
            if (!late.has_value())
            {
                late = bus.subscribe<Ping>([&late_received](const Ping&) { ++late_received; });
            }
        });

    bus.publish(Ping{});
    bus.dispatch_pending();
    trace("after the event that created it: late_received={}", late_received);
    CHECK(late_received == 0);

    bus.publish(Ping{});
    bus.dispatch_pending();
    trace("after the following event: late_received={}", late_received);
    CHECK(late_received == 1);
}

TEST_CASE("events published by a handler are delivered in the same drain", "[core][event_bus]")
{
    EventBus bus;
    std::string pong_text;

    const auto ping_subscription = bus.subscribe<Ping>(
        [&bus](const Ping& event)
        {
            if (event.value > 0)
            {
                bus.publish(Pong{.text = "from handler"});
            }
        });
    const auto pong_subscription =
        bus.subscribe<Pong>([&pong_text](const Pong& event) { pong_text = event.text; });

    bus.publish(Ping{.value = 1});
    bus.dispatch_pending();

    trace("second generation delivered: pong_text=\"{}\", pending={}", pong_text,
          bus.pending_count());
    CHECK(pong_text == "from handler");
    CHECK(bus.pending_count() == 0);
}

TEST_CASE("a handler feedback loop is capped rather than hanging the frame", "[core][event_bus]")
{
    EventBus bus;
    int deliveries = 0;

    const auto subscription = bus.subscribe<Ping>(
        [&bus, &deliveries](const Ping& event)
        {
            ++deliveries;
            bus.publish(Ping{.value = event.value + 1});
        });

    const cpen::test::LogCaptureGuard logs;
    bus.publish(Ping{.value = 0});
    bus.dispatch_pending();

    trace("deliveries={} (cap is {} passes), still pending={}", deliveries,
          EventBus::MAX_DISPATCH_PASSES, bus.pending_count());
    trace("reported as an error: {}", logs.contains("did not settle"));

    CHECK(deliveries == static_cast<int>(EventBus::MAX_DISPATCH_PASSES));
    CHECK(bus.pending_count() == 1);
    CHECK(logs.contains("did not settle"));

    // The backlog is kept, not dropped, so it can be cleared explicitly.
    bus.clear_pending();
    CHECK(bus.pending_count() == 0);
}

TEST_CASE("dispatch_pending refuses to run inside a handler", "[core][event_bus]")
{
    EventBus bus;
    const auto subscription = bus.subscribe<Ping>([&bus](const Ping&) { bus.dispatch_pending(); });

    const cpen::test::LogCaptureGuard logs;
    bus.publish(Ping{});
    bus.dispatch_pending();

    trace("recursive dispatch reported: {}", logs.contains("from inside a handler"));
    CHECK(logs.contains("from inside a handler"));
}

TEST_CASE("a subscription outliving its bus is inert", "[core][event_bus]")
{
    Subscription subscription;

    {
        EventBus bus;
        subscription = bus.subscribe<Ping>([](const Ping&) {});
        trace("while the bus is alive: active={}", subscription.active());
        CHECK(subscription.active());
    }

    trace("after the bus is destroyed: active={}", subscription.active());
    CHECK_FALSE(subscription.active());

    // Destroying the handle here must not touch the dead bus; the weak reference
    // is the whole point.
    subscription.release();
}

TEST_CASE("clear_subscribers leaves handles valid but inert", "[core][event_bus]")
{
    EventBus bus;
    int received = 0;
    const auto subscription = bus.subscribe<Ping>([&received](const Ping&) { ++received; });

    bus.clear_subscribers();
    bus.publish(Ping{});
    bus.dispatch_pending();

    trace("after clear_subscribers: received={}, subscribers={}", received, bus.subscriber_count());
    CHECK(received == 0);
    CHECK(bus.subscriber_count() == 0);
}
